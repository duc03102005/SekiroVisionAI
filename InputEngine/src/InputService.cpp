#include <SekiroVisionAI/InputService.h>
#include <shellapi.h>
#include <array>
#include <chrono>
#include <utility>

namespace sekiro {
namespace {
constexpr LONG watchdog_magic=0x53564149;
constexpr ULONG_PTR input_tag=0x53564149;
constexpr WORD scans[]{0x2a,0x1e,0x20,0x1f,0x11}; // Left Shift, A, D, S, W; physical scan codes.
using SubmitFunction=std::function<UINT(UINT,INPUT*,int)>;
UINT submit_events(UINT count,INPUT* events,const SubmitFunction* submit=nullptr) {
    return submit && *submit ? (*submit)(count,events,sizeof(INPUT)) : SendInput(count,events,sizeof(INPUT));
}
struct alignas(8) WatchdogState {
    LONG magic{}, parent_pid{};
    volatile LONG ready{}, quit{}, owned_mask{}, tripped{}, release_failed{};
    alignas(8) volatile LONG64 deadline_ms{};
};
LONG read(volatile LONG* p) { return InterlockedCompareExchange(p,0,0); }
bool release_owned(WatchdogState* state,const SubmitFunction* submit=nullptr) {
    const LONG mask=read(&state->owned_mask);
    if(!mask) return true;
    std::array<INPUT,5> events{}; UINT count=0;
    for(unsigned i=0;i<5;++i) if(mask&(1<<i)) {
        auto& event=events[count++]; event.type=INPUT_KEYBOARD; event.ki.wScan=scans[i];
        event.ki.dwFlags=KEYEVENTF_SCANCODE|KEYEVENTF_KEYUP;
        event.ki.dwExtraInfo=input_tag;
    }
    const auto sent=submit_events(count,events.data(),submit);
    // Retain ownership until key-up submission succeeds, so a parent crash cannot hide held keys.
    if(sent==count) { InterlockedCompareExchange(&state->owned_mask,0,mask); return true; }
    InterlockedExchange(&state->release_failed,1); return false;
}

class Watchdog {
public:
    explicit Watchdog(const SubmitFunction* submit=nullptr):submit_(submit) {
        name_=L"Local\\SekiroVisionAI-input-"+std::to_wstring(GetCurrentProcessId())+L"-"+
            std::to_wstring(GetCurrentThreadId())+L"-"+std::to_wstring(GetTickCount64());
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
            for(int i=0;i<3 && !release_owned(state_,submit_);++i) Sleep(10);
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
    const SubmitFunction* submit_{};
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
    EventCallback callback;
    mutable std::mutex mutex;
    InputStatus state;
    std::string detector_reason{"DETECTOR_STARTING"};
    WindowTarget selected;
    std::optional<DodgeRequest> pending;
    std::atomic_bool enabled{}, quit{};
    std::atomic_uint64_t revision{1};
    double latest_source{}, last_submission{-1e9}, cooldown{650}, arm_until{};
    int direction{}, hold_ms{45};
    HANDLE wake{CreateEventW(nullptr,FALSE,FALSE,nullptr)};
    SubmitFunction submit;
#ifdef SVAI_INPUT_TESTING
    InputTestHooks test;
#endif
    std::thread worker;

    explicit Impl(std::shared_ptr<EventLog> value,EventCallback event
#ifdef SVAI_INPUT_TESTING
        ,InputTestHooks hooks={}
#endif
    ):log(std::move(value)),callback(std::move(event)) {
#ifdef SVAI_INPUT_TESTING
        test=std::move(hooks); submit=test.send_input;
#endif
        if(!wake) throw std::runtime_error("Could not create input wake event");
        worker=std::thread([this]{run();});
    }
    ~Impl() {
        disable("APPLICATION_SHUTDOWN"); quit=true; SetEvent(wake);
        if(worker.joinable())worker.join(); CloseHandle(wake);
    }
    bool current(const WindowTarget& value) const {
#ifdef SVAI_INPUT_TESTING
        if(test.target_is_current)return test.target_is_current(value);
#endif
        return target_is_current(value);
    }
    // Called with mutex held. The same mutex serializes cancellation and the
    // final SendInput call, so a cancelled queued command cannot cross dispatch.
    bool cancel_locked(const std::string& reason) {
        const bool was=enabled.exchange(false)||state.arm_pending;
        ++revision; pending.reset(); state.arm_pending=false; arm_until=0; state.reason=reason;
        return was;
    }
    void announce_disabled(const std::string& reason,bool was) {
        if(was) {log->emit("AUTO_DISABLED",reason);if(callback)callback("INPUT_DISABLED: "+reason,0);}
        SetEvent(wake);
    }
    void disable(const std::string& reason) {
        bool was=false;
        { std::lock_guard lock(mutex);was=cancel_locked(reason); }
        announce_disabled(reason,was);
    }
    void reject(const std::string& reason,std::uint64_t episode) {
        {std::lock_guard lock(mutex);++state.rejected;state.reason=reason;}
        log->emit("DODGE_SUPPRESSED",reason,episode);
    }
    std::string arm_blocked_locked() const {
        if(!state.hotkeys_ready)return "HOTKEY_CONFLICT";
        if(!state.watchdog_ready)return "WATCHDOG_UNAVAILABLE";
        if(!state.capture_running)return "START_CAPTURE_FIRST";
        if(!state.detector_ready)return detector_reason;
        if(!selected.hwnd||!current(selected))return "TARGET_IDENTITY_CHANGED";
        if(down(VK_F9))return "RELEASE_F9_BEFORE_ARM";
        return {};
    }
    void enable_locked() {
        ++revision; pending.reset(); state.arm_pending=false; arm_until=0;
        state.reason="AUTO_ON"; enabled=true;
    }
    void toggle(bool allow_pending) {
        bool activated=false,deferred=false,cancelled=false; std::string blocked;
        const auto origin=allow_pending?"UI":"F8";
        {
            std::lock_guard lock(mutex);
            if(enabled||state.arm_pending)cancelled=cancel_locked(std::string(origin)+"_TOGGLE_OFF");
            else {
                blocked=arm_blocked_locked();
                if(blocked.empty()) {
                    const bool focused=selected.hwnd==GetForegroundWindow();
                    const bool fresh=fresh_at(latest_source,qpc_ms(),120);
                    if(focused&&fresh) {enable_locked();activated=true;}
                    else if(allow_pending) {
                        ++revision; pending.reset(); state.arm_pending=true;
                        arm_until=qpc_ms()+15000; state.reason="ARM_PENDING_RETURN_TO_GAME"; deferred=true;
                    } else blocked=focused?"CAPTURE_TOO_OLD_FOR_F8":"RETURN_TO_SEKIRO_THEN_F8";
                }
            }
        }
        if(cancelled)announce_disabled(std::string(origin)+"_TOGGLE_OFF",true);
        if(activated)log->emit("AUTO_ENABLED",std::string(origin)+"; temporal history restarts; wait for quiet before attack arming");
        if(deferred)log->emit("AUTO_ARM_PENDING","Explicit UI arm; waiting up to 15 seconds for selected game foreground and fresh capture");
        if(!blocked.empty())reject(std::string(origin)+"_BLOCKED: "+blocked,0);
        SetEvent(wake);
    }
    void poll_pending_arm() {
        bool activated=false,cancelled=false; std::string reason;
        {
            std::lock_guard lock(mutex);
            if(!state.arm_pending)return;
            reason=arm_blocked_locked();
            if(reason.empty()&&qpc_ms()>=arm_until)reason="ARM_PENDING_EXPIRED";
            if(!reason.empty())cancelled=cancel_locked(reason);
            else if(selected.hwnd==GetForegroundWindow()&&fresh_at(latest_source,qpc_ms(),120)) {
                enable_locked();activated=true;
            }
        }
        if(cancelled)announce_disabled(reason,true);
        if(activated)log->emit("AUTO_ENABLED","Pending UI arm activated in selected game; temporal history restarts");
    }
    void run() noexcept {
        bool f8=false,f9=false,f10=false,f4=false,f5=false,f6=false,f7=false;
        try {
            Watchdog watchdog(&submit);
            f8=RegisterHotKey(nullptr,8,MOD_NOREPEAT,VK_F8)!=FALSE;
            f9=RegisterHotKey(nullptr,9,MOD_NOREPEAT,VK_F9)!=FALSE;
            f10=RegisterHotKey(nullptr,10,MOD_NOREPEAT,VK_F10)!=FALSE;
            f4=RegisterHotKey(nullptr,4,MOD_NOREPEAT,VK_F4)!=FALSE;
            f5=RegisterHotKey(nullptr,5,MOD_NOREPEAT,VK_F5)!=FALSE;
            f6=RegisterHotKey(nullptr,6,MOD_NOREPEAT,VK_F6)!=FALSE;
            f7=RegisterHotKey(nullptr,7,MOD_NOREPEAT,VK_F7)!=FALSE;
            {
                std::lock_guard lock(mutex); state.hotkeys_ready=f8&&f9; state.watchdog_ready=watchdog.ready();
                state.reason=!state.hotkeys_ready?"HOTKEY_CONFLICT":!state.watchdog_ready?"WATCHDOG_UNAVAILABLE":"AUTO_OFF";
            }
            log->emit("INPUT_READY",std::string("F8/F9=")+(f8&&f9?"ready":"conflict")+" F10="+(f10?"ready":"conflict")+
                " watchdog="+(watchdog.ready()?"ready":"unavailable"));
            log->emit("RECORD_HOTKEYS",std::string("F4 miss=")+(f4?"ready":"conflict")+" F5 false-positive="+(f5?"ready":"conflict")+
                " F6 sample="+(f6?"ready":"conflict")+" F7 recording="+(f7?"ready":"conflict"));
            DispatchGuard auto_guard,manual_guard;
            std::uint64_t manual_episode=0;
            double release_at=0;
            bool watchdog_reported=false,emergency_was_down=false;
            unsigned release_failures=0;
            while(!quit) {
                MSG msg{};
                while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) if(msg.message==WM_HOTKEY) {
                    if(msg.wParam==9) disable("F9_EMERGENCY");
                    else if(msg.wParam==8)toggle(false);
                    else if(msg.wParam==10) {
                        std::lock_guard lock(mutex);
                        if(enabled&&!pending)pending=DodgeRequest{revision.load(),++manual_episode,latest_source,true};
                    } else if(msg.wParam>=4&&msg.wParam<=7&&callback) {
                        bool foreground=false;
                        {std::lock_guard lock(mutex);foreground=state.capture_running&&selected.hwnd==GetForegroundWindow()&&current(selected);}
                        if(foreground)callback(msg.wParam==4?"MISSED_ATTACK_REVIEW":msg.wParam==5?"FALSE_POSITIVE_REVIEW":
                            msg.wParam==6?"MANUAL_SAMPLE":"TOGGLE_RECORDING",0);
                    }
                }
                // A registered unmodified F9 does not fire while Shift is held
                // by our chord. Poll the actual key too, independently of the UI.
                const bool emergency_down=down(VK_F9);
                if(emergency_down&&!emergency_was_down)disable("F9_EMERGENCY");
                emergency_was_down=emergency_down;
                bool capture=false,focus=false,detector=false; double source=0;
                {
                    std::lock_guard lock(mutex);state.watchdog_ready=watchdog.ready()&&!watchdog_reported;
                    capture=state.capture_running;focus=selected.hwnd&&selected.hwnd==GetForegroundWindow()&&current(selected);
                    source=latest_source;detector=state.detector_ready;
                }
                if(enabled && (!capture||!focus||!detector||!fresh_at(source,qpc_ms(),150)||!watchdog.ready()))
                    disable(!focus?"LOST_FOCUS":!watchdog.ready()?"WATCHDOG_EXITED":!detector?"DETECTOR_UNAVAILABLE":"CAPTURE_STALE_OR_STOPPED");
                auto* owned=watchdog.state();
                if(owned&&read(&owned->tripped)&&!watchdog_reported) {
                    watchdog_reported=true;
                    {std::lock_guard lock(mutex);state.watchdog_ready=false;}
                    disable("WATCHDOG_RELEASED_KEYS");log->emit("WATCHDOG_RELEASE","Parent input deadline expired or release failed");
                }
                poll_pending_arm();
                if(owned&&read(&owned->owned_mask)&&release_failures<3&&(!enabled||qpc_ms()>=release_at)) {
                    const bool ok=release_owned(owned,&submit);
                    log->emit(ok?"KEY_UP_SUBMITTED":"KEY_UP_FAILED",ok?"Released owned Shift/direction keys":"Windows rejected key-up; watchdog will retry");
                    if(!ok) {++release_failures;disable("INPUT_RELEASE_FAILED");}
                }
                std::optional<DodgeRequest> request;
                {std::lock_guard lock(mutex);request=std::exchange(pending,{});}
                if(request) {
                    if(request->direction < -1 || request->direction > 4) {reject("INVALID_DODGE_DIRECTION",request->episode);continue;}
                    if(!std::isfinite(request->earliest_send_ms)||!std::isfinite(request->latest_send_ms)||
                       request->earliest_send_ms<0||request->latest_send_ms<0||
                       (request->latest_send_ms==0&&request->earliest_send_ms!=0)||
                       (request->latest_send_ms>0&&request->earliest_send_ms>request->latest_send_ms)) {
                        reject("INVALID_TTI_WINDOW",request->episode);continue;
                    }
#ifdef SVAI_INPUT_TESTING
                    if(test.before_submit)test.before_submit();
#endif
                    UINT sent=0,count=0; DWORD error=ERROR_SUCCESS; LONG mask=1;
                    int dir=0,hold=45; bool player_moving=false; const char* reason=nullptr;
                    bool submission_attempted=false,submission_failed=false,was_disabled=false;
                    double source_age=unavailable;
                    {
                        std::lock_guard lock(mutex);
                        const double now=qpc_ms();
                        dir=request->direction<0?direction:request->direction;hold=hold_ms;
                        const bool modifiers=down(VK_SHIFT)||down(VK_CONTROL)||down(VK_MENU)||down(VK_LWIN)||down(VK_RWIN);
                        const bool foreground=selected.hwnd==GetForegroundWindow()&&current(selected);
                        DispatchContext context{enabled.load(),foreground,state.capture_running&&state.detector_ready,
                            state.watchdog_ready&&watchdog.ready()&&!watchdog_reported,
                            modifiers||(owned&&read(&owned->owned_mask)),request->source_ms,now,cooldown,
                            request->revision,revision.load(),request->episode};
                        // Fault/disable/focus always take precedence over cooldown.
                        if(!context.enabled)reason="DISABLED";
                        else if(!context.foreground)reason="LOST_FOCUS";
                        else if(!context.capture_running||!context.watchdog_ready)reason="CAPTURE_OR_DETECTOR_OR_WATCHDOG_UNAVAILABLE";
                        else if(!fresh_at(latest_source,now,120)||!fresh_at(request->source_ms,now,120))reason="STALE_FRAME";
                        else if(context.now_ms-last_submission<cooldown)reason="GLOBAL_COOLDOWN";
                        else if(!request->manual&&request->latest_send_ms>0&&
                            (now<request->earliest_send_ms||now>request->latest_send_ms))reason="TTI_WINDOW_EXPIRED_BEFORE_SEND";
                        else reason=(request->manual?manual_guard:auto_guard).reserve(context);
                        if(!reason) {
                            std::array<INPUT,2> events{};
                            player_moving=down('W')||down('A')||down('S')||down('D');
                            if(dir>0&&!player_moving) {
                                events[count].type=INPUT_KEYBOARD;events[count].ki.wScan=scans[dir];
                                events[count].ki.dwExtraInfo=input_tag;events[count++].ki.dwFlags=KEYEVENTF_SCANCODE;mask|=1<<dir;
                            }
                            events[count].type=INPUT_KEYBOARD;events[count].ki.wScan=scans[0];
                            events[count].ki.dwExtraInfo=input_tag;events[count++].ki.dwFlags=KEYEVENTF_SCANCODE;
                            const double dispatch_now=qpc_ms();
                            // OS focus cannot be locked atomically with SendInput. Recheck it
                            // immediately, without logging/callbacks between this check and send.
                            if(down(VK_F9)||GetForegroundWindow()!=selected.hwnd||
                               !fresh_at(request->source_ms,dispatch_now,120)||
                               (!request->manual&&request->latest_send_ms>0&&dispatch_now>request->latest_send_ms))reason="CANCELLED_BEFORE_SEND";
                            else {
                                InterlockedExchange64(&owned->deadline_ms,static_cast<LONG64>(GetTickCount64()+250));
                                release_failures=0;
                                InterlockedExchange(&owned->owned_mask,mask); // Recovery ownership precedes key-down.
                                SetLastError(ERROR_SUCCESS);
                                sent=submit_events(count,events.data(),&submit);error=GetLastError();
                                submission_attempted=true;source_age=dispatch_now-request->source_ms;
                                last_submission=qpc_ms();release_at=last_submission+hold;
                                if(sent!=count) {
                                    submission_failed=true;
                                    // SendInput inserts the array serially. Retain only
                                    // the accepted prefix after a partial insertion.
                                    LONG accepted_mask=0;
                                    for(UINT i=0;i<std::min(sent,count);++i)
                                        for(unsigned key=0;key<5;++key)
                                            if(events[i].ki.wScan==scans[key])accepted_mask|=1<<key;
                                    InterlockedAnd(&owned->owned_mask,accepted_mask);
                                    was_disabled=cancel_locked("SENDINPUT_FAILED");
                                } else {++state.sent;state.reason="DODGE_SENT";}
                            }
                        }
                    }
                    if(reason) {reject(reason,request->episode);continue;}
                    if(submission_attempted) {
                        log->emit(request->manual?"MANUAL_DODGE_REQUEST":"DODGE_REQUEST","source_age_ms="+std::to_string(source_age)+
                            " direction="+std::to_string(dir)+" owned_mask="+std::to_string(mask)+" frame="+std::to_string(request->sequence)+
                            " model="+request->model_version+" "+request->prediction_details,request->episode);
                        if(submission_failed) {
                            log->emit("SENDINPUT_FAILED","inserted="+std::to_string(sent)+" requested="+std::to_string(count)+" win32="+std::to_string(error),request->episode);
                            announce_disabled("SENDINPUT_FAILED",was_disabled);release_owned(owned,&submit);
                        } else {
                            log->emit("DODGE_SENT","inserted="+std::to_string(sent)+" hold_ms="+std::to_string(hold)+
                                " direction="+std::to_string(dir)+(player_moving?" player_direction_preserved":" selected_direction"),request->episode);
                            if(callback)callback(request->manual?"MANUAL_DODGE_SENT":"DODGE_SENT",request->episode);
                        }
                    }
                }
                MsgWaitForMultipleObjects(1,&wake,FALSE,5,QS_ALLINPUT);
            }
            disable("INPUT_WORKER_STOPPED");
            if(watchdog.state())release_owned(watchdog.state(),&submit);
        } catch(const std::exception& e) {disable("INPUT_WORKER_FAULT");log->emit("INPUT_FAULT",e.what());}
        catch(...) {disable("INPUT_WORKER_FAULT");}
        {std::lock_guard lock(mutex);state.hotkeys_ready=false;state.watchdog_ready=false;}
        if(f8)UnregisterHotKey(nullptr,8);
        if(f9)UnregisterHotKey(nullptr,9);
        if(f10)UnregisterHotKey(nullptr,10);
        if(f4)UnregisterHotKey(nullptr,4);
        if(f5)UnregisterHotKey(nullptr,5);
        if(f6)UnregisterHotKey(nullptr,6);
        if(f7)UnregisterHotKey(nullptr,7);
    }
};

InputService::InputService(std::shared_ptr<EventLog> log,EventCallback callback):impl_(std::make_unique<Impl>(std::move(log),std::move(callback))){}
#ifdef SVAI_INPUT_TESTING
InputService::InputService(std::shared_ptr<EventLog> log,EventCallback callback,InputTestHooks hooks):
    impl_(std::make_unique<Impl>(std::move(log),std::move(callback),std::move(hooks))){}
#endif
InputService::~InputService()=default;
void InputService::target(const WindowTarget& target) {
    bool was=false;
    {std::lock_guard lock(impl_->mutex);was=impl_->cancel_locked("NEW_CAPTURE_TARGET");
     impl_->selected=target;impl_->state.capture_running=true;impl_->latest_source=0;}
    impl_->announce_disabled("NEW_CAPTURE_TARGET",was);
}
void InputService::frame(double source_ms) {
    const auto now=qpc_ms();bool invalid=false,was=false;
    {
        std::lock_guard lock(impl_->mutex);
        if(!std::isfinite(age_ms(source_ms,now))) {
            invalid=true;impl_->latest_source=0;was=impl_->cancel_locked("CAPTURE_TIMESTAMP_INVALID");
        } else if(source_ms>impl_->latest_source) {
            impl_->latest_source=source_ms;impl_->state.capture_running=true;
        }
    }
    if(invalid)impl_->announce_disabled("CAPTURE_TIMESTAMP_INVALID",was);
    SetEvent(impl_->wake);
}
void InputService::toggle(){impl_->toggle(true);}
void InputService::disable(const std::string& reason){impl_->disable(reason);}
void InputService::capture_stopped(){
    bool was=false;
    {std::lock_guard lock(impl_->mutex);impl_->state.capture_running=false;impl_->latest_source=0;
     was=impl_->cancel_locked("CAPTURE_DISCONTINUITY");}
    impl_->announce_disabled("CAPTURE_DISCONTINUITY",was);
}
void InputService::configure(int direction,int hold_ms,double cooldown_ms){
    bool was=false;
    {
        std::lock_guard lock(impl_->mutex);was=impl_->cancel_locked("CONFIG_CHANGED");
        impl_->direction=std::clamp(direction,0,4);impl_->hold_ms=std::clamp(hold_ms,30,90);
        impl_->cooldown=std::isfinite(cooldown_ms)?std::clamp(cooldown_ms,250.0,2000.0):650;
    }
    impl_->announce_disabled("CONFIG_CHANGED",was);
}
void InputService::request(DodgeRequest request){
    {std::lock_guard lock(impl_->mutex);if(!impl_->pending)impl_->pending=std::move(request);}
    SetEvent(impl_->wake);
}
void InputService::detector_ready(bool ready,const std::string& reason){
    bool was=false,changed=false;
    {
        std::lock_guard lock(impl_->mutex);changed=impl_->state.detector_ready!=ready;
        impl_->state.detector_ready=ready;impl_->detector_reason=reason;
        if(!ready&&(changed||impl_->enabled||impl_->state.arm_pending))was=impl_->cancel_locked(reason);
    }
    if(!ready&&changed)impl_->announce_disabled(reason,was);
    SetEvent(impl_->wake);
}
InputStatus InputService::status()const{
    std::lock_guard lock(impl_->mutex);auto result=impl_->state;
    result.enabled=impl_->enabled;result.revision=impl_->revision;return result;
}
}
