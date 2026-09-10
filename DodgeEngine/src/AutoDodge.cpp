#include <SekiroVisionAI/AutoDodge.h>
#include <chrono>
#include <utility>

namespace sekiro {
namespace {
MvpConfig read_config(const std::filesystem::path& path) {
    MvpConfig c;
    auto get=[&](const wchar_t* section,const wchar_t* key,int value){return static_cast<int>(GetPrivateProfileIntW(section,key,value,path.c_str()));};
    c.roi.left=get(L"roi",L"left_per_mille",280)/1000.0;c.roi.top=get(L"roi",L"top_per_mille",120)/1000.0;
    c.roi.right=get(L"roi",L"right_per_mille",720)/1000.0;c.roi.bottom=get(L"roi",L"bottom_per_mille",610)/1000.0;
    if(!c.roi.valid())c.roi={};
    c.threat.enter_score=std::clamp(get(L"detector",L"enter_percent",62),35,95)/100.0;
    c.threat.exit_score=std::clamp(get(L"detector",L"exit_percent",40),10,static_cast<int>(c.threat.enter_score*100)-5)/100.0;
    c.threat.min_confidence=std::clamp(get(L"detector",L"confidence_percent",55),30,95)/100.0;
    c.threat.dwell_ms=std::clamp(get(L"detector",L"arming_ms",40),20,250);
    c.threat.cooldown_ms=std::clamp(get(L"detector",L"cooldown_ms",650),250,2000);
    c.threat.quiet_ms=std::clamp(get(L"detector",L"quiet_ms",220),120,1000);
    c.direction=std::clamp(get(L"input",L"direction",0),0,4);c.hold_ms=std::clamp(get(L"input",L"hold_ms",45),30,90);
    c.preset=std::clamp(get(L"detector",L"preset",1),0,2);
    return c;
}
bool save_config(const std::filesystem::path& path,const MvpConfig& c) {
    bool ok=true;
    auto put=[&](const wchar_t* section,const wchar_t* key,int value){
        if(!WritePrivateProfileStringW(section,key,std::to_wstring(value).c_str(),path.c_str()))ok=false;
    };
    put(L"meta",L"schema_version",1);
    put(L"roi",L"left_per_mille",static_cast<int>(std::lround(c.roi.left*1000)));put(L"roi",L"top_per_mille",static_cast<int>(std::lround(c.roi.top*1000)));
    put(L"roi",L"right_per_mille",static_cast<int>(std::lround(c.roi.right*1000)));put(L"roi",L"bottom_per_mille",static_cast<int>(std::lround(c.roi.bottom*1000)));
    put(L"detector",L"enter_percent",static_cast<int>(std::lround(c.threat.enter_score*100)));
    put(L"detector",L"exit_percent",static_cast<int>(std::lround(c.threat.exit_score*100)));
    put(L"detector",L"confidence_percent",static_cast<int>(std::lround(c.threat.min_confidence*100)));
    put(L"detector",L"arming_ms",static_cast<int>(c.threat.dwell_ms));put(L"detector",L"cooldown_ms",static_cast<int>(c.threat.cooldown_ms));
    put(L"detector",L"quiet_ms",static_cast<int>(c.threat.quiet_ms));put(L"detector",L"preset",c.preset);
    put(L"input",L"direction",c.direction);put(L"input",L"hold_ms",c.hold_ms);
    return ok;
}
}

struct AutoDodge::Impl {
    std::shared_ptr<EventLog> events=std::make_shared<EventLog>();
    InputService input{events};
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool quit{},accepting{};
    std::optional<SmallFrame> newest;
    std::uint64_t last_sequence{},last_generation{},blocked_generation{};
    MvpSnapshot state;
    std::filesystem::path config_file=mvp_data_directory()/L"mvp.ini";
    MvpConfig settings=read_config(config_file);
    std::thread worker;

    Impl() {
        input.configure(settings.direction,settings.hold_ms,settings.threat.cooldown_ms);
        if(!save_config(config_file,settings))events->emit("CONFIG_SAVE_FAILED","Using in-memory settings");
        events->emit("MVP_START","CV heuristic v1; no trained model; 256x144 gray; F8 toggle, F9 disable, F10 manual test; build " SVAI_BUILD_REVISION);
        worker=std::thread([this]{run();});
    }
    ~Impl() {
        input.capture_stopped();
        {std::lock_guard lock(mutex);quit=true;accepting=false;newest.reset();}
        cv.notify_one(); if(worker.joinable())worker.join();
    }
    void run() noexcept {
        MotionDetector detector;ThreatTracker tracker;
        std::uint64_t revision=0,generation=0;
        double last_log=0;
        std::string last_state;
        while(true) {
            SmallFrame frame;MvpConfig cfg;
            {std::unique_lock lock(mutex);cv.wait(lock,[&]{return quit||newest.has_value();});if(quit)break;
             frame=std::move(*newest);newest.reset();cfg=settings;}
            try {
                const auto input_state=input.status();
                if(revision!=input_state.revision || generation!=frame.generation) {
                    detector.reset();tracker.reset();revision=input_state.revision;generation=frame.generation;last_state.clear();
                }
                const double begin=qpc_ms();
                auto motion=detector.process(frame,cfg.roi);
                if(std::string(motion.reason)=="SAMPLE_INTERVAL")continue;
                const double now=qpc_ms();
                auto threat=tracker.step(motion,now,cfg.threat);
                if(input.status().revision!=revision)continue; // Ignore work computed across an arm/disable/config change.
                {
                    std::lock_guard lock(mutex);
                    if(!accepting||frame.generation<=blocked_generation)continue;
                    state.preview=frame;state.has_frame=true;state.motion=motion;state.threat=threat;
                    ++state.processed;state.cpu_ms=now-begin;state.frame_age_ms=now-frame.source_ms;
                    if(threat.trigger)++state.threats;
                }
                if(last_state!=threat.state) {
                    events->emit("THREAT_STATE",std::string(threat.state)+" reason="+threat.reason,threat.episode);
                    last_state=threat.state;
                }
                if(now-last_log>=200||threat.trigger) {
                    std::ostringstream values;values.imbue(std::locale::classic());values<<std::fixed<<std::setprecision(3)
                        <<"score="<<motion.score<<" confidence="<<motion.confidence<<" changed="<<motion.changed_fraction
                        <<" flow="<<motion.flow<<" accel="<<motion.acceleration<<" camera="<<motion.camera_dx<<","<<motion.camera_dy
                        <<" bg_error="<<motion.background_error<<" frame_age_ms="<<now-frame.source_ms
                        <<" cv_ms="<<now-begin<<" reason="<<threat.reason;
                    events->emit(threat.trigger?"THREAT_READY":"CV_SIGNALS",values.str(),threat.episode);last_log=now;
                }
                if(threat.trigger) input.request({revision,threat.episode,frame.source_ms,false});
            } catch(const std::exception& error) {input.disable("VISION_FAULT");events->emit("VISION_FAULT",error.what());detector.reset();tracker.reset();}
            catch(...) {input.disable("VISION_FAULT");detector.reset();tracker.reset();}
        }
    }
};

AutoDodge::AutoDodge():impl_(std::make_unique<Impl>()){}
AutoDodge::~AutoDodge()=default;
void AutoDodge::start(const WindowTarget& target) {
    impl_->input.target(target);
    {std::lock_guard lock(impl_->mutex);impl_->newest.reset();impl_->state={};impl_->last_sequence=impl_->last_generation=impl_->blocked_generation=0;impl_->accepting=true;}
    impl_->events->emit("CAPTURE_SELECTED","sekiro.exe pid="+std::to_string(target.pid));
}
void AutoDodge::stop() {
    {std::lock_guard lock(impl_->mutex);impl_->accepting=false;impl_->newest.reset();}
    impl_->input.capture_stopped();
}
void AutoDodge::discontinuity() {
    {std::lock_guard lock(impl_->mutex);impl_->blocked_generation=impl_->last_generation;impl_->newest.reset();}
    impl_->input.capture_stopped();
}
void AutoDodge::submit(const SmallFrame& frame) {
    {
        std::lock_guard lock(impl_->mutex);
        if(!impl_->accepting||frame.generation<=impl_->blocked_generation||
           (frame.generation==impl_->last_generation&&frame.sequence<=impl_->last_sequence))return;
        if(impl_->newest)++impl_->state.replaced;
        impl_->last_generation=frame.generation;impl_->last_sequence=frame.sequence;impl_->newest=frame;
    }
    impl_->input.frame(frame.source_ms);impl_->cv.notify_one();
}
void AutoDodge::disable(){impl_->input.disable("UI_DISABLE");}
MvpConfig AutoDodge::config()const{std::lock_guard lock(impl_->mutex);return impl_->settings;}
void AutoDodge::configure(MvpConfig config) {
    if(!config.roi.valid())return;
    config.direction=std::clamp(config.direction,0,4);config.hold_ms=std::clamp(config.hold_ms,30,90);
    impl_->input.configure(config.direction,config.hold_ms,config.threat.cooldown_ms);
    {std::lock_guard lock(impl_->mutex);impl_->settings=config;impl_->newest.reset();}
    if(!save_config(impl_->config_file,config))impl_->events->emit("CONFIG_SAVE_FAILED","Using in-memory settings");
    impl_->events->emit("CONFIG_CHANGED","ROI/threshold/direction changed; Auto Dodge disabled; F8 to re-arm");
}
MvpSnapshot AutoDodge::snapshot()const{MvpSnapshot s;{std::lock_guard lock(impl_->mutex);s=impl_->state;}s.input=impl_->input.status();return s;}
std::shared_ptr<EventLog> AutoDodge::log()const{return impl_->events;}
std::filesystem::path AutoDodge::config_path()const{return impl_->config_file;}
}
