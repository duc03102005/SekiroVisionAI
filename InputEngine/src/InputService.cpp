#include <SekiroVisionAI/InputService.h>
#include <shellapi.h>
#include <array>
#include <chrono>
#include <utility>

namespace sekiro {
namespace {
constexpr LONG watchdog_magic=0x53564149;
constexpr WORD scans[]{0x2a,0x1e,0x20,0x1f,0x11}; // Left Shift, A, D, S, W; physical scan codes.
struct alignas(8) WatchdogState {
    LONG magic{}, parent_pid{};
    volatile LONG ready{}, quit{}, owned_mask{}, tripped{}, release_failed{};
    alignas(8) volatile LONG64 deadline_ms{};
};
LONG read(volatile LONG* p) { return InterlockedCompareExchange(p,0,0); }
bool release_owned(WatchdogState* state) {
    const LONG mask=read(&state->owned_mask);
    if(!mask) return true;
    std::array<INPUT,5> events{}; UINT count=0;
    for(unsigned i=0;i<5;++i) if(mask&(1<<i)) {
        auto& event=events[count++]; event.type=INPUT_KEYBOARD; event.ki.wScan=scans[i];
        event.ki.dwFlags=KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
    }
    const auto sent=SendInput(count,events.data(),sizeof(INPUT));
    // Retain ownership until key-up submission succeeds, so a parent crash cannot hide held keys.
    if(sent==count) { InterlockedCompareExchange(&state->owned_mask,0,mask); return true; }
    InterlockedExchange(&state->release_failed,1); return false;
}

class Watchdog {
public:
    Watchdog() {
        name_=L"Local\\SekiroVisionAI-input-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
        mapping_=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(WatchdogState),name_.c_str());
        if(!mapping_) return;
        state_=static_cast<WatchdogState*>(MapViewOfFile(mapping_,FILE_MAP_ALL_ACCESS,0,0,sizeof(WatchdogState)));
        if(!state_) return;
        *state_={}; state_->magic=watchdog_magic; state_->parent_pid=static_cast<LONG>(GetCurrentProcessId());
        std::array<wchar_t,32768> exe{};
        if(!GetModuleFileNameW(nullptr,exe.data(),static_cast<DWORD>(exe.size()))) return;
        std::wstring command=L"\""+std::wstring(exe.data())+L"\" --input-watchdog \""+name_+L"\"";
        STARTUPINFOW startup{}; startup.cb=sizeof(startup); PROCESS_INFORMATION process{};
        if(!CreateProcessW(exe.data(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)) return;
        process_=process.hProcess; CloseHandle(process.hThread);
        const auto deadline=GetTickCount64()+2000;
        while(!read(&state_->ready)&&GetTickCount64()<deadline&&WaitForSingleObject(process_,10)==WAIT_TIMEOUT) {}
    }
    ~Watchdog() {
        if(state_) {
            for(int i=0;i<3 && !release_owned(state_);++i) Sleep(10);
            InterlockedExchange(&state_->quit,1);
        }
        if(process_) { WaitForSingleObject(process_,600); CloseHandle(process_); }
        if(state_) UnmapViewOfFile(state_);
        if(mapping_) CloseHandle(mapping_);
    }
    bool ready() const { return state_&&process_&&read(&state_->ready)&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT; }
    WatchdogState* state() { return state_; }
private:
    std::wstring name_;
    HANDLE mapping_{},process_{};
    WatchdogState* state_{};
};
bool down(int key) { return (GetAsyncKeyState(key)&0x8000)!=0; }
}

std::optional<int> input_watchdog_command_line() {
    int count{}; auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if(!args) return {};
    if(count!=3 || std::wstring(args[1])!=L"--input-watchdog") { LocalFree(args); return {}; }
    const std::wstring name=args[2]; LocalFree(args);
    if(name.rfind(L"Local\\SekiroVisionAI-input-",0)!=0) return 2;
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
    if(!mapping) return 3;
    auto* state=static_cast<WatchdogState*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(WatchdogState)));
    if(!state) { CloseHandle(mapping); return 4; }
    if(state->magic!=watchdog_magic) { UnmapViewOfFile(state); CloseHandle(mapping); return 5; }
    HANDLE parent=OpenProcess(SYNCHRONIZE,FALSE,static_cast<DWORD>(state->parent_pid));
    if(!parent) { UnmapViewOfFile(state); CloseHandle(mapping); return 6; }
    InterlockedExchange(&state->ready,1);
    for(;;) {
        const bool exited=WaitForSingleObject(parent,5)==WAIT_OBJECT_0;
        const bool late=static_cast<LONG64>(GetTickCount64())>=InterlockedCompareExchange64(&state->deadline_ms,0,0);
        if(read(&state->owned_mask) && (exited||late||read(&state->quit))) {
            InterlockedExchange(&state->tripped,1);
            for(int i=0;i<6 && !release_owned(state);++i) Sleep(20);
            if(read(&state->owned_mask)) break; // Bounded retries; parent detects watchdog exit and reports failure.
        }
        if(exited||read(&state->quit)) break;
    }
    CloseHandle(parent); UnmapViewOfFile(state); CloseHandle(mapping); return 0;
}

struct InputService::Impl {
    std::shared_ptr<EventLog> log;
    mutable std::mutex mutex;
    InputStatus state;
    WindowTarget selected;
    std::optional<DodgeRequest> pending;
    std::atomic_bool enabled{}, quit{};
    std::atomic_uint64_t revision{1};
    double latest_source{}, last_submission{-1e9}, cooldown{650};
    int direction{}, hold_ms{45};
    HANDLE wake{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    std::thread worker;

    explicit Impl(std::shared_ptr<EventLog> value):log(std::move(value)) {
        if(!wake) throw std::runtime_error("Could not create input wake event");
        worker=std::thread([this]{run();});
    }
    ~Impl() { enabled=false; quit=true; SetEvent(wake); if(worker.joinable())worker.join(); CloseHandle(wake); }
    void disable(const std::string& reason) {
        const bool was=enabled.exchange(false);
        ++revision;
        { std::lock_guard lock(mutex); pending.reset(); state.reason=reason; }
        if(was) log->emit("AUTO_DISABLED",reason);
        SetEvent(wake);
    }
    void reject(const std::string& reason,std::uint64_t episode) {
        {std::lock_guard lock(mutex);++state.rejected;state.reason=reason;}
        log->emit("DODGE_SUPPRESSED",reason,episode);
    }
    void run() noexcept {
        try {
            Watchdog watchdog;
            const bool f8=RegisterHotKey(nullptr,8,MOD_NOREPEAT,VK_F8)!=FALSE;
            const bool f9=RegisterHotKey(nullptr,9,MOD_NOREPEAT,VK_F9)!=FALSE;
            const bool f10=RegisterHotKey(nullptr,10,MOD_NOREPEAT,VK_F10)!=FALSE;
            {
                std::lock_guard lock(mutex); state.hotkeys_ready=f8&&f9&&f10; state.watchdog_ready=watchdog.ready();
                state.reason=!state.hotkeys_ready?"HOTKEY_CONFLICT":!state.watchdog_ready?"WATCHDOG_UNAVAILABLE":"AUTO_OFF_PRESS_F8_IN_GAME";
            }
            log->emit("INPUT_READY",std::string("F8/F9/F10=")+(f8&&f9&&f10?"ready":"conflict")+" watchdog="+(watchdog.ready()?"ready":"unavailable"));
            DispatchGuard auto_guard,manual_guard;
            std::uint64_t manual_episode=0;
            double release_at=0;
            bool watchdog_reported=false;
            unsigned release_failures=0;
            while(!quit) {
                MSG msg{};
                while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) if(msg.message==WM_HOTKEY) {
                    if(msg.wParam==9) disable("F9_EMERGENCY");
                    else if(msg.wParam==8) {
                        if(enabled) disable("F8_TOGGLE_OFF");
                        else {
                            bool allowed=false;
                            { std::lock_guard lock(mutex);
                              allowed=state.hotkeys_ready&&watchdog.ready()&&!watchdog_reported&&state.capture_running&&selected.hwnd==GetForegroundWindow()&&
                                target_is_current(selected)&&latest_source>0&&qpc_ms()-latest_source<120;
                              if(allowed) { ++revision; pending.reset(); state.reason="AUTO_ON"; enabled=true; }
                            }
                            if(allowed) { log->emit("AUTO_ENABLED","F8; temporal history restarts; wait for quiet before attack arming"); MessageBeep(MB_OK); }
                            else reject("F8_REQUIRES_FOREGROUND_SEKIRO_FRESH_CAPTURE_AND_HOTKEYS",0);
                        }
                    } else if(msg.wParam==10) {
                        std::lock_guard lock(mutex);
                        pending=DodgeRequest{revision.load(),++manual_episode,latest_source,true};
                    }
                }
                bool capture=false,focus=false; double source=0;
                {std::lock_guard lock(mutex);state.watchdog_ready=watchdog.ready()&&!watchdog_reported;
                 capture=state.capture_running;focus=selected.hwnd&&selected.hwnd==GetForegroundWindow();source=latest_source;}
                if(enabled && (!capture||!focus||qpc_ms()-source>150||!watchdog.ready()))
                    disable(!focus?"LOST_FOCUS":!watchdog.ready()?"WATCHDOG_EXITED":"CAPTURE_STALE_OR_STOPPED");
                auto* owned=watchdog.state();
                if(owned&&read(&owned->tripped)&&!watchdog_reported) {
                    watchdog_reported=true; disable("WATCHDOG_RELEASED_KEYS");log->emit("WATCHDOG_RELEASE","Parent input deadline expired or release failed");
                }
                if(owned&&read(&owned->owned_mask)&&release_failures<3&&(!enabled||qpc_ms()>=release_at)) {
                    const bool ok=release_owned(owned);
                    log->emit(ok?"KEY_UP_SUBMITTED":"KEY_UP_FAILED",ok?"Released owned Shift/direction keys":"Windows rejected key-up; watchdog will retry");
                    if(!ok) {++release_failures;disable("INPUT_RELEASE_FAILED");}
                }
                std::optional<DodgeRequest> request;
                {std::lock_guard lock(mutex);request=std::exchange(pending,{});}
                if(request) {
                    WindowTarget target; int dir=0,hold=45; double minimum=650;
                    {std::lock_guard lock(mutex);target=selected;dir=direction;hold=hold_ms;minimum=cooldown;}
                    const bool modifiers=down(VK_SHIFT)||down(VK_CONTROL)||down(VK_MENU)||down(VK_LWIN)||down(VK_RWIN);
                    const bool foreground=target.hwnd==GetForegroundWindow()&&target_is_current(target);
                    DispatchContext context{enabled.load(),foreground,capture,watchdog.ready()&&!watchdog_reported,
                        modifiers||(owned&&read(&owned->owned_mask)),request->source_ms,qpc_ms(),minimum,
                        request->revision,revision.load(),request->episode};
                    const char* reason=context.now_ms-last_submission<minimum?"GLOBAL_COOLDOWN":
                        (request->manual?manual_guard:auto_guard).reserve(context);
                    if(reason) { reject(reason,request->episode); continue; }
                    // Check cancellation and focus again directly before actual submission.
                    if(!enabled||request->revision!=revision||GetForegroundWindow()!=target.hwnd) {reject("CANCELLED_BEFORE_SEND",request->episode);continue;}
                    std::array<INPUT,2> events{}; UINT count=0; LONG mask=1;
                    const bool player_moving=down('W')||down('A')||down('S')||down('D');
                    if(dir>0 && !player_moving) {
                        events[count].type=INPUT_KEYBOARD;events[count].ki.wScan=scans[dir];events[count++].ki.dwFlags=KEYEVENTF_SCANCODE;
                        mask|=1<<dir;
                    }
                    events[count].type=INPUT_KEYBOARD;events[count].ki.wScan=scans[0];events[count++].ki.dwFlags=KEYEVENTF_SCANCODE;
                    log->emit(request->manual?"MANUAL_DODGE_REQUEST":"DODGE_REQUEST","source_age_ms="+std::to_string(context.now_ms-request->source_ms)+" owned_mask="+std::to_string(mask),request->episode);
                    InterlockedExchange64(&owned->deadline_ms,static_cast<LONG64>(GetTickCount64()+250));
                    release_failures=0;
                    InterlockedExchange(&owned->owned_mask,mask); // Child owns recovery responsibility before key-down.
                    if(!enabled||request->revision!=revision||GetForegroundWindow()!=target.hwnd) {
                        release_owned(owned);reject("CANCELLED_BEFORE_SEND",request->episode);continue;
                    }
                    SetLastError(ERROR_SUCCESS);
                    const UINT sent=SendInput(count,events.data(),sizeof(INPUT));
                    const DWORD error=GetLastError();
                    last_submission=qpc_ms(); release_at=last_submission+hold;
                    if(sent!=count) {
                        log->emit("SENDINPUT_FAILED","inserted="+std::to_string(sent)+" requested="+std::to_string(count)+" win32="+std::to_string(error),request->episode);
                        disable("SENDINPUT_FAILED");release_owned(owned);
                    } else {
                        {std::lock_guard lock(mutex);++state.sent;state.reason="DODGE_SENT";}
                        log->emit("DODGE_SENT","inserted="+std::to_string(sent)+" hold_ms="+std::to_string(hold)+(player_moving?" player_direction_preserved":" configured_direction"),request->episode);
                    }
                }
                MsgWaitForMultipleObjects(1,&wake,FALSE,5,QS_ALLINPUT);
            }
            enabled=false;
            if(watchdog.state()) release_owned(watchdog.state());
            if(f8)UnregisterHotKey(nullptr,8);if(f9)UnregisterHotKey(nullptr,9);if(f10)UnregisterHotKey(nullptr,10);
        } catch(const std::exception& e) {disable("INPUT_WORKER_FAULT");log->emit("INPUT_FAULT",e.what());}
        catch(...) {disable("INPUT_WORKER_FAULT");}
    }
};

InputService::InputService(std::shared_ptr<EventLog> log):impl_(std::make_unique<Impl>(std::move(log))){}
InputService::~InputService()=default;
void InputService::target(const WindowTarget& target) {
    impl_->disable("NEW_CAPTURE_TARGET");
    std::lock_guard lock(impl_->mutex);impl_->selected=target;impl_->state.capture_running=true;impl_->latest_source=0;
}
void InputService::frame(double source_ms) {std::lock_guard lock(impl_->mutex);impl_->latest_source=std::max(impl_->latest_source,source_ms);impl_->state.capture_running=true;}
void InputService::disable(const std::string& reason){impl_->disable(reason);}
void InputService::capture_stopped(){impl_->disable("CAPTURE_DISCONTINUITY");std::lock_guard lock(impl_->mutex);impl_->state.capture_running=false;}
void InputService::configure(int direction,int hold_ms,double cooldown_ms){
    impl_->disable("CONFIG_CHANGED");std::lock_guard lock(impl_->mutex);
    impl_->direction=std::clamp(direction,0,4);impl_->hold_ms=std::clamp(hold_ms,30,90);impl_->cooldown=std::clamp(cooldown_ms,250.0,2000.0);
}
void InputService::request(DodgeRequest request){std::lock_guard lock(impl_->mutex);if(!impl_->pending)impl_->pending=request;SetEvent(impl_->wake);}
InputStatus InputService::status()const{std::lock_guard lock(impl_->mutex);auto result=impl_->state;result.enabled=impl_->enabled;result.revision=impl_->revision;return result;}
}
