#include <SekiroVisionAI/InputService.h>
#include <shellapi.h>
#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>

// This target is never distributed as the application. Its only permitted
// input target is the benign receiver process created by this test.
#ifndef SVAI_INPUT_TESTING
#error Build InputServiceTests and InputService.cpp with SVAI_INPUT_TESTING=1.
#endif
namespace {
constexpr ULONG_PTR app_tag=0x53564149;
constexpr ULONG_PTR test_tag=0x53565453;
constexpr LONG receiver_magic=0x49545356;
HWND test_receiver{};
std::array<bool,256> test_owned{};
struct KeyEvent { ULONGLONG time{}; UINT message{}; WORD scan{}; ULONG_PTR tag{}; };
struct ReceiverState {
    LONG magic{};
    volatile LONG ready{},count{},quit{};
    HWND hwnd{}; DWORD pid{};
    std::array<KeyEvent,512> events{};
};
LONG read(volatile LONG* value) {return InterlockedCompareExchange(value,0,0);}
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
void pump() {
    MSG msg{};
    while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {TranslateMessage(&msg);DispatchMessageW(&msg);}
}
bool until(const std::function<bool()>& predicate,DWORD milliseconds=2000) {
    const auto end=GetTickCount64()+milliseconds;
    do {pump();if(predicate())return true;Sleep(2);}while(GetTickCount64()<end);
    pump();return predicate();
}
void delay(DWORD milliseconds) {until([]{return false;},milliseconds);}
LRESULT CALLBACK receiver_proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    auto* state=reinterpret_cast<ReceiverState*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));
    if(msg==WM_NCCREATE) {
        state=static_cast<ReceiverState*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
        SetWindowLongPtrW(hwnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(state));
    }
    if(state&&(msg==WM_KEYDOWN||msg==WM_KEYUP||msg==WM_SYSKEYDOWN||msg==WM_SYSKEYUP)) {
        const auto n=read(&state->count);
        if(n>=0&&n<static_cast<LONG>(state->events.size())) {
            state->events[static_cast<std::size_t>(n)]={GetTickCount64(),msg,static_cast<WORD>((l>>16)&0xff),
                static_cast<ULONG_PTR>(GetMessageExtraInfo())};
            InterlockedIncrement(&state->count);
        }
        return 0;
    }
    if(msg==WM_TIMER&&state&&read(&state->quit)) {DestroyWindow(hwnd);return 0;}
    if(msg==WM_DESTROY) {PostQuitMessage(0);return 0;}
    return DefWindowProcW(hwnd,msg,w,l);
}
int receiver_main(const std::wstring& name) {
    if(name.rfind(L"Local\\SekiroVisionAI-input-test-",0)!=0)return 2;
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
    if(!mapping)return 3;
    auto* state=static_cast<ReceiverState*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ReceiverState)));
    if(!state||state->magic!=receiver_magic) {if(state)UnmapViewOfFile(state);CloseHandle(mapping);return 4;}
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=receiver_proc;
    wc.lpszClassName=L"SekiroVisionAIInputTestReceiver";
    RegisterClassW(&wc);
    const auto hwnd=CreateWindowW(wc.lpszClassName,L"SekiroVisionAI controlled input test receiver",WS_OVERLAPPEDWINDOW,
        40,40,600,200,nullptr,nullptr,wc.hInstance,state);
    if(!hwnd) {UnmapViewOfFile(state);CloseHandle(mapping);return 5;}
    ShowWindow(hwnd,SW_SHOW);SetTimer(hwnd,1,10,nullptr);
    state->hwnd=hwnd;state->pid=GetCurrentProcessId();InterlockedExchange(&state->ready,1);
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) {TranslateMessage(&msg);DispatchMessageW(&msg);}
    UnmapViewOfFile(state);CloseHandle(mapping);return 0;
}
std::uint64_t creation(HANDLE process) {
    FILETIME c{},e{},k{},u{};
    require(GetProcessTimes(process,&c,&e,&k,&u)!=FALSE,"GetProcessTimes failed");
    return (static_cast<std::uint64_t>(c.dwHighDateTime)<<32)|c.dwLowDateTime;
}
HANDLE launch(const std::wstring& mode,const std::wstring& argument) {
    std::array<wchar_t,32768> path{};
    require(GetModuleFileNameW(nullptr,path.data(),static_cast<DWORD>(path.size()))!=0,"GetModuleFileName failed");
    std::wstring command=L"\""+std::wstring(path.data())+L"\" "+mode+L" \""+argument+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    require(CreateProcessW(path.data(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)!=FALSE,
        "Could not launch controlled test child");
    CloseHandle(process.hThread);return process.hProcess;
}
struct Receiver {
    std::wstring name=L"Local\\SekiroVisionAI-input-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HANDLE mapping{},process{};ReceiverState* state{};
    Receiver() {
        mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(ReceiverState),name.c_str());
        require(mapping!=nullptr,"Receiver mapping failed");
        state=static_cast<ReceiverState*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ReceiverState)));
        require(state!=nullptr,"Receiver mapping view failed");
        *state={};state->magic=receiver_magic;process=launch(L"--input-test-receiver",name);
        require(until([&]{return read(&state->ready)!=0;},4000),"Receiver window failed to start");
    }
    ~Receiver() {
        if(state)InterlockedExchange(&state->quit,1);
        if(process) {WaitForSingleObject(process,1000);CloseHandle(process);}
        if(state)UnmapViewOfFile(state);
        if(mapping)CloseHandle(mapping);
    }
    sekiro::WindowTarget target() const {return {state->hwnd,state->pid,creation(process),L"Controlled test receiver"};}
    bool current(const sekiro::WindowTarget& t) const {
        DWORD pid{};GetWindowThreadProcessId(t.hwnd,&pid);
        return t.hwnd==state->hwnd&&pid==state->pid&&IsWindowVisible(t.hwnd)&&
            t.process_creation_time==creation(process)&&WaitForSingleObject(process,0)==WAIT_TIMEOUT;
    }
    LONG count() const {return read(&state->count);}
    unsigned keys(LONG after,WORD scan,bool up) const {
        unsigned found=0;const auto end=count();
        for(LONG i=after;i<end;++i) {
            const auto& e=state->events[static_cast<std::size_t>(i)];
            const bool is_up=e.message==WM_KEYUP||e.message==WM_SYSKEYUP;
            if(e.tag==app_tag&&e.scan==scan&&is_up==up)++found;
        }
        return found;
    }
    unsigned downs(LONG after) const {
        unsigned found=0;const auto end=count();
        for(LONG i=after;i<end;++i) {
            const auto& e=state->events[static_cast<std::size_t>(i)];
            if(e.tag==app_tag&&(e.message==WM_KEYDOWN||e.message==WM_SYSKEYDOWN))++found;
        }
        return found;
    }
};
bool focus(HWND hwnd) {
    ShowWindow(hwnd,SW_SHOW);SetForegroundWindow(hwnd);
    return until([&]{return GetForegroundWindow()==hwnd;},800);
}
void key(WORD vk,bool up) {
    require(vk<test_owned.size(),"Invalid test key");
    require(up||GetForegroundWindow()==test_receiver,"Test control key target lost foreground");
    INPUT event{};event.type=INPUT_KEYBOARD;event.ki.wVk=vk;event.ki.dwExtraInfo=test_tag;
    event.ki.dwFlags=up?KEYEVENTF_KEYUP:0;
    require(SendInput(1,&event,sizeof(INPUT))==1,"Test control key was rejected by Windows");
    test_owned[vk]=!up;
}
struct TestKeyCleanup {
    ~TestKeyCleanup() {
        for(unsigned vk=0;vk<test_owned.size();++vk)if(test_owned[vk]) {
            INPUT event{};event.type=INPUT_KEYBOARD;event.ki.wVk=static_cast<WORD>(vk);
            event.ki.dwFlags=KEYEVENTF_KEYUP;event.ki.dwExtraInfo=test_tag;
            SendInput(1,&event,sizeof(INPUT));test_owned[vk]=false;
        }
    }
};
struct SubmissionControl {
    std::atomic_bool block_before{},partial_next{},stall_after_send{};
    HANDLE entered{CreateEventW(nullptr,TRUE,FALSE,nullptr)},resume{CreateEventW(nullptr,TRUE,FALSE,nullptr)};
    ~SubmissionControl() {SetEvent(resume);CloseHandle(entered);CloseHandle(resume);}
    void reset() {ResetEvent(entered);ResetEvent(resume);}
    void before() {
        if(block_before.exchange(false)) {SetEvent(entered);WaitForSingleObject(resume,2000);}
    }
    UINT send(UINT count,INPUT* events,int size) {
        const bool down=count>0&&(events[0].ki.dwFlags&KEYEVENTF_KEYUP)==0;
        if(down&&count>1&&partial_next.exchange(false))return SendInput(1,events,size);
        const auto sent=SendInput(count,events,size);
        if(down&&stall_after_send.exchange(false)) {SetEvent(entered);WaitForSingleObject(resume,2000);}
        return sent;
    }
};
sekiro::DodgeRequest request(sekiro::InputService& input,std::uint64_t episode,int direction=1) {
    sekiro::DodgeRequest value;
    value.revision=input.status().revision;value.episode=episode;value.source_ms=sekiro::qpc_ms();
    value.direction=direction;value.model_version="INPUT_INTEGRATION_TEST_NO_GAMEPLAY_MODEL";
    return value;
}
void report(const char* name) {std::cout<<"INPUT_TEST_OK "<<name<<'\n';}

int crash_parent_main(const std::wstring& name) {
    if(name.rfind(L"Local\\SekiroVisionAI-input-test-",0)!=0)return 2;
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
    if(!mapping)return 3;
    auto* state=static_cast<ReceiverState*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(ReceiverState)));
    if(!state||state->magic!=receiver_magic)return 4;
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,state->pid);
    if(!process)return 5;
    sekiro::WindowTarget target{state->hwnd,state->pid,creation(process),L"Controlled test receiver"};
    sekiro::InputTestHooks hooks;
    hooks.target_is_current=[&](const sekiro::WindowTarget& t) {
        DWORD pid{};GetWindowThreadProcessId(t.hwnd,&pid);
        return t.hwnd==target.hwnd&&pid==target.pid&&t.process_creation_time==creation(process)&&
            WaitForSingleObject(process,0)==WAIT_TIMEOUT;
    };
    hooks.send_input=[](UINT count,INPUT* events,int size) {
        const auto sent=SendInput(count,events,size);
        if(sent==count&&count>0&&(events[0].ki.dwFlags&KEYEVENTF_KEYUP)==0) {
            // Deliberate test-only process death after actual accepted key-down;
            // no destructor runs. The independent production watchdog must release.
            TerminateProcess(GetCurrentProcess(),73);
        }
        return sent;
    };
    auto log=std::make_shared<sekiro::EventLog>();sekiro::InputService input(log,{},std::move(hooks));
    if(!until([&]{const auto s=input.status();return s.watchdog_ready&&s.hotkeys_ready;},4000))return 6;
    input.configure(1,90,250);input.target(target);input.detector_ready(true,"TEST_READY");
    input.frame(sekiro::qpc_ms());input.toggle();
    if(!input.status().enabled)return 7;
    input.request(request(input,1));
    delay(1000);return 8;
}
}

int main() {
    if(const auto watchdog=sekiro::input_watchdog_command_line())return *watchdog;
    int count{};auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(args&&count==3) {
        const std::wstring mode=args[1],name=args[2];LocalFree(args);
        if(mode==L"--input-test-receiver")return receiver_main(name);
        if(mode==L"--input-test-crash-parent")return crash_parent_main(name);
    } else if(args)LocalFree(args);
    try {
        HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
        if(!desktop) {std::cout<<"INPUT_TEST_UNAVAILABLE no interactive input desktop; SendInput not verified\n";return 77;}
        CloseDesktop(desktop);
        const auto control=CreateWindowW(L"STATIC",L"SekiroVisionAI controlled test host",WS_OVERLAPPEDWINDOW,
            680,40,360,200,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        require(control!=nullptr,"Control window creation failed");
        Receiver receiver;
        test_receiver=receiver.state->hwnd;TestKeyCleanup test_key_cleanup;
        if(!focus(control)) {
            std::cout<<"INPUT_TEST_UNAVAILABLE Windows denied test-host foreground; SendInput not verified\n";
            DestroyWindow(control);return 77;
        }
        SubmissionControl submission;
        sekiro::InputTestHooks hooks;
        hooks.target_is_current=[&](const sekiro::WindowTarget& t){return receiver.current(t);};
        hooks.before_submit=[&]{submission.before();};
        hooks.send_input=[&](UINT n,INPUT* e,int s){return submission.send(n,e,s);};
        auto log=std::make_shared<sekiro::EventLog>();
        auto input=std::make_unique<sekiro::InputService>(log,sekiro::InputService::EventCallback{},std::move(hooks));
        if(!until([&]{auto s=input->status();return s.watchdog_ready&&s.hotkeys_ready;},4000)) {
            std::cout<<"INPUT_TEST_UNAVAILABLE "<<input->status().reason<<"; SendInput not verified\n";
            input.reset();DestroyWindow(control);return 77;
        }
        input->configure(3,80,250);input->target(receiver.target());
        std::atomic_bool feeding{true};
        std::jthread feeder([&](std::stop_token stop){while(!stop.stop_requested()) {
            if(feeding)input->frame(sekiro::qpc_ms());
            Sleep(8);
        }});
        require(until([&]{return input->status().capture_running;}),"No controlled capture state");
        input->toggle();require(!input->status().enabled&&!input->status().arm_pending,"Missing detector armed input");
        input->detector_ready(true,"TEST_READY");delay(15);input->toggle();
        require(input->status().arm_pending&&!input->status().enabled,"UI arm must wait for target foreground");
        delay(40);require(!input->status().enabled,"Pending UI arm sent to application UI");
        if(!focus(receiver.state->hwnd)) {
            feeder.request_stop();feeder.join();input.reset();DestroyWindow(control);
            std::cout<<"INPUT_TEST_UNAVAILABLE Windows denied receiver foreground; SendInput not verified\n";return 77;
        }
        require(until([&]{return input->status().enabled;}),"Pending UI arm failed to activate on fresh selected target");
        report("explicit UI pending arm + detector readiness + foreground");
        key(VK_F8,false);delay(15);key(VK_F8,true);
        require(until([&]{return !input->status().enabled;}),"Actual F8 did not toggle OFF");
        key(VK_F8,false);delay(15);key(VK_F8,true);
        require(until([&]{return input->status().enabled;}),"Actual F8 did not toggle ON");
        report("actual registered F8 toggles Auto Dodge OFF/ON");

        const auto first=receiver.count();input->request(request(*input,1,1));
        require(until([&]{return receiver.keys(first,0x2a,true)==1&&receiver.keys(first,0x1e,true)==1;}),"Actual A+Shift did not release");
        require(receiver.keys(first,0x2a,false)==1&&receiver.keys(first,0x1e,false)==1&&receiver.keys(first,0x1f,false)==0,
            "Per-request A override did not replace configured S");
        ULONGLONG pressed=0,released=0;
        for(LONG i=first;i<receiver.count();++i) {
            const auto& e=receiver.state->events[static_cast<std::size_t>(i)];
            if(e.tag==app_tag&&e.scan==0x2a) {
                if(e.message==WM_KEYDOWN)pressed=e.time;
                if(e.message==WM_KEYUP)released=e.time;
            }
        }
        require(pressed>0&&released>=pressed+25&&released<=pressed+500,"Delivered chord hold outside bounded test window");
        std::cout<<"INPUT_TEST_MEASURED receiver Shift hold_ms="<<released-pressed<<" configured_ms=80\n";
        report("actual SendInput A + Shift key-down/key-up and direction override");
        auto rejected=input->status().rejected;input->request(request(*input,2));
        require(until([&]{return input->status().rejected>rejected;}),"Cooldown did not suppress immediate repeat");
        delay(280);const auto no_more=receiver.count();rejected=input->status().rejected;input->request(request(*input,1));
        require(until([&]{return input->status().rejected>rejected;}),"Consumed episode was not rejected");
        require(receiver.downs(no_more)==0,"Repeated consumed episode sent another Dodge");
        report("global cooldown + one dispatch per episode");

        for(int variant=0;variant<5;++variant) {
            auto value=request(*input,3+static_cast<unsigned>(variant));
            if(variant==0)value.source_ms=sekiro::qpc_ms()-500;
            if(variant==1)value.source_ms=std::numeric_limits<double>::quiet_NaN();
            if(variant==2)value.source_ms=sekiro::qpc_ms()+1000;
            if(variant==3)value.latest_send_ms=std::numeric_limits<double>::infinity();
            if(variant==4)value.direction=5;
            rejected=input->status().rejected;input->request(value);
            require(until([&]{return input->status().rejected>rejected;}),"Invalid request was not rejected");
        }
        require(receiver.downs(no_more)==0,"Malformed/stale/future command reached SendInput");
        report("stale, NaN, future, invalid TTI and invalid direction reject");
        feeding=false;delay(15);input->frame(std::numeric_limits<double>::quiet_NaN());
        require(!input->status().enabled&&!input->status().arm_pending,"Invalid capture clock did not latch off");
        feeding=true;delay(25);require(!input->status().enabled,"Fresh capture silently re-armed after invalid clock");
        report("invalid capture clock disables and requires explicit re-arm");

        input->toggle();require(until([&]{return input->status().enabled;}),"Re-arm failed");
        input->configure(1,90,250);input->toggle();
        const auto emergency=receiver.count();input->request(request(*input,10,2));
        require(until([&]{return receiver.keys(emergency,0x2a,false)==1;}),"No chord to test F9");
        key(VK_F9,false);delay(20);key(VK_F9,true);
        require(until([&]{return !input->status().enabled&&receiver.keys(emergency,0x2a,true)==1&&receiver.keys(emergency,0x20,true)==1;}),
            "F9 during owned Shift did not disable and release");
        delay(50);require(!input->status().enabled,"F9 emergency did not latch OFF");
        report("actual F9 during Shift chord releases and latches OFF");
        delay(270);

        const auto cancelled=receiver.count();
        for(int kind=0;kind<3;++kind) {
            require(focus(receiver.state->hwnd),"Could not focus controlled receiver for cancellation test");
            input->detector_ready(true,"TEST_READY");input->toggle();
            require(until([&]{return input->status().enabled;}),"Cancellation test could not arm");
            submission.reset();submission.block_before=true;input->request(request(*input,20+static_cast<unsigned>(kind)));
            require(until([&]{return WaitForSingleObject(submission.entered,0)==WAIT_OBJECT_0;}),"Dispatch barrier not entered");
            if(kind==0)input->disable("TEST_CANCELLED");
            if(kind==1)require(focus(control),"Could not focus controlled host for focus-loss test");
            if(kind==2)input->detector_ready(false,"TEST_MODEL_FAILURE");
            rejected=input->status().rejected;SetEvent(submission.resume);
            require(until([&]{return input->status().rejected>rejected&&!input->status().enabled;}),"Queued command crossed disable/focus/model fault");
        }
        require(receiver.downs(cancelled)==0,"Queued cancelled action inserted keys");
        report("queued command cancellation on disable, focus loss and model failure");

        require(focus(receiver.state->hwnd),"Could not focus receiver for physical-key conflict");
        input->detector_ready(true,"TEST_READY");input->toggle();
        require(until([&]{return input->status().enabled;}),"Physical-key test could not arm");
        key(VK_LSHIFT,false);delay(10);rejected=input->status().rejected;input->request(request(*input,30));
        require(until([&]{return input->status().rejected>rejected;}),"Physical Shift conflict not rejected");
        key(VK_LSHIFT,true);delay(10);require(receiver.downs(cancelled)==0,"Physical modifier conflict inserted keys");
        const auto moving=receiver.count();key('W',false);delay(10);input->request(request(*input,31,1));
        require(until([&]{return receiver.keys(moving,0x2a,true)==1;}),"Dodge with existing player movement failed");
        require(receiver.keys(moving,0x1e,false)==0&&receiver.keys(moving,0x11,true)==0,"Engine overrode or released player-owned movement");
        key('W',true);report("physical modifier conflict + player direction ownership");delay(280);

        const auto partial=receiver.count();const auto sent_before=input->status().sent;
        submission.partial_next=true;input->request(request(*input,40,1));
        require(until([&]{return !input->status().enabled&&receiver.keys(partial,0x1e,true)==1;}),"Partial insertion did not release accepted A");
        require(input->status().sent==sent_before&&receiver.keys(partial,0x1e,false)==1&&receiver.keys(partial,0x2a,false)==0&&
            receiver.keys(partial,0x2a,true)==0,"Partial insertion counted a Dodge or released an unowned Shift");
        report("partial SendInput insertion fails closed and releases accepted key only");delay(280);

        input->toggle();require(until([&]{return input->status().enabled;}),"Could not arm watchdog stall test");
        const auto stalled=receiver.count();submission.reset();submission.stall_after_send=true;
        input->request(request(*input,50,2));
        require(until([&]{return WaitForSingleObject(submission.entered,0)==WAIT_OBJECT_0;}),"Accepted-send stall not reached");
        const bool child_released=until([&]{return receiver.keys(stalled,0x2a,true)==1&&receiver.keys(stalled,0x20,true)==1;},1200);
        SetEvent(submission.resume);require(child_released,"Independent watchdog failed to release while input thread stalled");
        require(until([&]{return !input->status().enabled&&!input->status().watchdog_ready;}),"Watchdog trip did not latch input fault");
        report("independent watchdog releases during stalled input thread");
        feeder.request_stop();feeder.join();input.reset();

        require(focus(receiver.state->hwnd),"Could not focus receiver for process-death watchdog test");
        const auto crashed=receiver.count();HANDLE crashed_parent=launch(L"--input-test-crash-parent",receiver.name);
        require(until([&]{return WaitForSingleObject(crashed_parent,0)==WAIT_OBJECT_0;},6000),"Deliberate crash parent did not exit");
        DWORD exit{};GetExitCodeProcess(crashed_parent,&exit);CloseHandle(crashed_parent);
        require(exit==73,"Crash parent did not reach accepted key-down before deliberate process death");
        require(until([&]{return receiver.keys(crashed,0x2a,true)==1&&receiver.keys(crashed,0x1e,true)==1;},1500),
            "Independent watchdog failed after parent process death");
        require(receiver.keys(crashed,0x2a,false)==1&&receiver.keys(crashed,0x1e,false)==1,"Process-death test had no actual accepted chord");
        report("independent watchdog releases after parent process termination");
        DestroyWindow(control);
        std::cout<<"INPUT_TEST_RESULT verified actual Windows SendInput receiver delivery and release; no Sekiro/gameplay timing claim\n";
        return 0;
    } catch(const std::exception& e) {
        std::cerr<<"INPUT_TEST_FAILED "<<e.what()<<'\n';return 1;
    }
}
