#include <SekiroVisionAI/AutoDodge.h>
#include <chrono>
#include <utility>

namespace sekiro {
namespace {
bool supports_auto(const ModelStatus& info){return info.loaded&&info.trained&&info.attack_supported&&info.threat_supported&&info.tti_supported&&info.auto_eligible;}
std::string model_arm_reason(const ModelStatus& info){
    if(!info.loaded)return info.reason;
    if(!info.trained)return "MODEL_NOT_GAMEPLAY_TRAINED";
    if(!supports_auto(info))return "UNSUPPORTED_ATTACK_THREAT_TTI_HEADS_OR_MODEL_NO_AUTO";
    return "MODEL_READY";
}
std::filesystem::path bundled_model_path() {
    std::wstring value(32768,L'\0');const auto length=GetModuleFileNameW(nullptr,value.data(),static_cast<DWORD>(value.size()));
    if(!length||length>=value.size())return {};
    value.resize(length);return std::filesystem::path(value).parent_path()/L"models"/L"attack-v0.1"/L"model.onnx";
}
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
    c.detector_mode=std::clamp(get(L"model",L"mode",1),0,1);
    std::wstring model_path(32768,L'\0');
    model_path.resize(GetPrivateProfileStringW(L"model",L"path",bundled_model_path().c_str(),model_path.data(),static_cast<DWORD>(model_path.size()),path.c_str()));
    c.model_path=model_path;
    const int provider=get(L"model",L"provider",1);c.provider=provider==2?"CUDA":provider==1?"DirectML":"CPU";
    c.temporal.attack_threshold=std::clamp(get(L"model",L"attack_percent",80),50,99)/100.0;
    c.temporal.threat_threshold=std::clamp(get(L"model",L"threat_percent",80),50,99)/100.0;
    c.temporal.exit_threshold=std::clamp(get(L"model",L"exit_percent",35),5,45)/100.0;
    c.temporal.lead_min_ms=std::clamp(get(L"model",L"lead_min_ms",70),10,200);
    c.temporal.lead_max_ms=std::clamp(get(L"model",L"lead_max_ms",130),static_cast<int>(c.temporal.lead_min_ms)+10,400);
    c.temporal.max_uncertainty_ms=std::clamp(get(L"model",L"max_uncertainty_ms",25),0,100);
    c.temporal.dwell_ms=std::clamp(get(L"model",L"dwell_ms",33),15,200);
    c.temporal.quiet_ms=std::clamp(get(L"model",L"quiet_ms",180),100,1000);
    c.temporal.cooldown_ms=std::clamp(get(L"model",L"cooldown_ms",450),250,2000);
    return c;
}
bool save_config(const std::filesystem::path& path,const MvpConfig& c) {
    bool ok=true;
    auto put=[&](const wchar_t* section,const wchar_t* key,int value){
        if(!WritePrivateProfileStringW(section,key,std::to_wstring(value).c_str(),path.c_str()))ok=false;
    };
    put(L"meta",L"schema_version",2);
    put(L"roi",L"left_per_mille",static_cast<int>(std::lround(c.roi.left*1000)));put(L"roi",L"top_per_mille",static_cast<int>(std::lround(c.roi.top*1000)));
    put(L"roi",L"right_per_mille",static_cast<int>(std::lround(c.roi.right*1000)));put(L"roi",L"bottom_per_mille",static_cast<int>(std::lround(c.roi.bottom*1000)));
    put(L"detector",L"enter_percent",static_cast<int>(std::lround(c.threat.enter_score*100)));
    put(L"detector",L"exit_percent",static_cast<int>(std::lround(c.threat.exit_score*100)));
    put(L"detector",L"confidence_percent",static_cast<int>(std::lround(c.threat.min_confidence*100)));
    put(L"detector",L"arming_ms",static_cast<int>(c.threat.dwell_ms));put(L"detector",L"cooldown_ms",static_cast<int>(c.threat.cooldown_ms));
    put(L"detector",L"quiet_ms",static_cast<int>(c.threat.quiet_ms));put(L"detector",L"preset",c.preset);
    put(L"input",L"direction",c.direction);put(L"input",L"hold_ms",c.hold_ms);
    put(L"model",L"mode",c.detector_mode);put(L"model",L"provider",c.provider=="CUDA"?2:c.provider=="DirectML"?1:0);
    if(!WritePrivateProfileStringW(L"model",L"path",c.model_path.c_str(),path.c_str()))ok=false;
    put(L"model",L"attack_percent",static_cast<int>(std::lround(c.temporal.attack_threshold*100)));
    put(L"model",L"threat_percent",static_cast<int>(std::lround(c.temporal.threat_threshold*100)));
    put(L"model",L"exit_percent",static_cast<int>(std::lround(c.temporal.exit_threshold*100)));
    put(L"model",L"lead_min_ms",static_cast<int>(c.temporal.lead_min_ms));put(L"model",L"lead_max_ms",static_cast<int>(c.temporal.lead_max_ms));
    put(L"model",L"max_uncertainty_ms",static_cast<int>(c.temporal.max_uncertainty_ms));put(L"model",L"dwell_ms",static_cast<int>(c.temporal.dwell_ms));
    put(L"model",L"quiet_ms",static_cast<int>(c.temporal.quiet_ms));put(L"model",L"cooldown_ms",static_cast<int>(c.temporal.cooldown_ms));
    return ok;
}
}

struct AutoDodge::Impl {
    std::shared_ptr<EventLog> events=std::make_shared<EventLog>();
    SampleRecorder recorder{mvp_data_directory()/L"training_samples",[this](const std::string& name,const std::string& detail){events->emit(name,detail);}};
    InputService input{events,[this](const std::string& name,std::uint64_t episode){
        if(name=="TOGGLE_RECORDING")recorder.enabled(!recorder.status().enabled);
        else if(name.rfind("INPUT_DISABLED",0)==0)recorder.discontinuity(name);
        else recorder.mark(name,episode);
    }};
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool quit{},accepting{},reload{true};
    std::optional<SmallFrame> newest;
    std::uint64_t last_sequence{},last_generation{},blocked_generation{};
    MvpSnapshot state;
    std::filesystem::path config_file=mvp_data_directory()/L"mvp.ini";
    MvpConfig settings=read_config(config_file);
    std::thread worker;

    Impl() {
        input.configure(settings.direction,settings.hold_ms,settings.detector_mode?settings.temporal.cooldown_ms:settings.threat.cooldown_ms);
        input.detector_ready(settings.detector_mode==0,"MODEL_STARTING");
        if(!save_config(config_file,settings))events->emit("CONFIG_SAVE_FAILED","Using in-memory settings");
        events->emit("MVP_START","Temporal ONNX + explicit CV fallback; BGRA color max1280x720; F8 toggle/F9 disable/F10 test; build " SVAI_BUILD_REVISION);
        worker=std::thread([this]{run();});
    }
    ~Impl() {
        input.capture_stopped();
        {std::lock_guard lock(mutex);quit=true;accepting=false;newest.reset();}
        cv.notify_one(); if(worker.joinable())worker.join();
    }
    void run() noexcept {
        MotionDetector detector;ThreatTracker tracker;TemporalDecision temporal;TemporalModel model;
        std::uint64_t revision=0,generation=0;
        double last_log=0;
        std::string last_state;
        while(true) {
            SmallFrame frame;MvpConfig cfg;bool load=false,has_frame=false;
            {std::unique_lock lock(mutex);cv.wait(lock,[&]{return quit||reload||newest.has_value();});if(quit)break;
             cfg=settings;load=std::exchange(reload,false);
             if(newest){frame=std::move(*newest);newest.reset();has_frame=true;}}
            try {
                if(load) {
                    model.load(cfg.model_path,cfg.provider);detector.reset();tracker.reset();temporal.reset();
                    const auto info=model.status();
                    input.detector_ready(cfg.detector_mode==0||supports_auto(info),model_arm_reason(info));
                    {std::lock_guard lock(mutex);state.model=info;}
                    events->emit(info.loaded?"MODEL_LOADED":"MODEL_UNAVAILABLE",info.version+" provider="+info.provider+" "+info.reason);
                }
                if(!has_frame)continue;
                const auto input_state=input.status();
                if(revision!=input_state.revision || generation!=frame.generation) {
                    detector.reset();tracker.reset();temporal.reset();model.reset_history();revision=input_state.revision;generation=frame.generation;last_state.clear();
                }
                const double begin=qpc_ms();
                auto motion=detector.process(frame,cfg.roi);
                auto prediction=cfg.detector_mode?model.process(frame,cfg.roi):ModelPrediction{};
                const double now=qpc_ms();
                prediction.prediction_ms=now;
                if(cfg.detector_mode&&prediction.reason=="MODEL_SAMPLE_INTERVAL") {
                    std::lock_guard lock(mutex);
                    if(accepting&&frame.generation>blocked_generation&&input.status().revision==revision){
                        state.preview=frame;state.has_frame=true;state.motion=motion;
                    }
                    continue;
                }
                auto action=TemporalAction{};
                ThreatDecision threat;
                if(cfg.detector_mode) {
                    if(prediction.valid) {action=temporal.step(prediction,now,cfg.temporal);threat=action.decision;}
                    else {threat.state="MODEL_WAIT";threat.reason="NO_VALID_MODEL_PREDICTION";}
                    const auto info=model.status();
                    input.detector_ready(supports_auto(info),model_arm_reason(info));
                } else {
                    if(std::string(motion.reason)=="SAMPLE_INTERVAL")continue;
                    threat=tracker.step(motion,now,cfg.threat);
                }
                if(input.status().revision!=revision)continue; // Ignore work computed across an arm/disable/config change.
                {
                    std::lock_guard lock(mutex);
                    if(!accepting||frame.generation<=blocked_generation)continue;
                    state.preview=frame;state.has_frame=true;state.motion=motion;state.threat=threat;
                    state.prediction=prediction;state.model=model.status();
                    ++state.processed;state.cpu_ms=now-begin;state.frame_age_ms=now-frame.source_ms;
                    if(threat.trigger)++state.threats;
                }
                if(last_state!=threat.state) {
                    events->emit("THREAT_STATE",std::string(threat.state)+" reason="+threat.reason+" frame="+std::to_string(frame.sequence),threat.episode);
                    last_state=threat.state;
                }
                if(now-last_log>=200||threat.trigger) {
                    std::ostringstream values;values.imbue(std::locale::classic());values<<std::fixed<<std::setprecision(3)
                        <<"score="<<motion.score<<" confidence="<<motion.confidence<<" changed="<<motion.changed_fraction
                        <<" flow="<<motion.flow<<" accel="<<motion.acceleration<<" camera="<<motion.camera_dx<<","<<motion.camera_dy
                        <<" bg_error="<<motion.background_error<<" frame_age_ms="<<now-frame.source_ms
                        <<" cv_ms="<<now-begin<<" reason="<<threat.reason;
                    if(cfg.detector_mode)values<<" model="<<prediction.version<<" frame="<<frame.sequence<<" source_ms="<<frame.source_ms
                        <<" valid="<<prediction.valid<<" attack_p="<<(prediction.valid?std::to_string(prediction.attack_probability):"null")
                        <<" threat_p="<<(prediction.valid&&prediction.threat_supported?std::to_string(prediction.threat_probability):"null")
                        <<" tti_supported="<<prediction.tti_supported<<" tti_ms="<<(prediction.valid&&prediction.tti_supported?std::to_string(prediction.tti_ms):"null")
                        <<" uncertainty_ms="<<(prediction.valid&&prediction.tti_supported?std::to_string(prediction.tti_uncertainty_ms):"null")
                        <<" attack_class="<<attack_classes[prediction.attack_class]<<" boss=UNKNOWN distance=UNKNOWN"
                        <<" model_ms="<<prediction.inference_ms<<" model_reason="<<prediction.reason;
                    events->emit(threat.trigger?"THREAT_READY":cfg.detector_mode?"MODEL_SIGNALS":"CV_SIGNALS",values.str(),threat.episode);last_log=now;
                }
                if(threat.trigger) {
                    DodgeRequest request;request.revision=revision;request.episode=threat.episode;request.source_ms=frame.source_ms;request.sequence=frame.sequence;
                    request.earliest_send_ms=action.earliest_send_ms;request.latest_send_ms=action.latest_send_ms;
                    request.model_version=cfg.detector_mode?prediction.version:"CV_HEURISTIC_FALLBACK";
                    if(cfg.detector_mode)request.prediction_details="attack_p="+std::to_string(prediction.attack_probability)+" threat_p="+std::to_string(prediction.threat_probability)+
                        " tti_ms="+std::to_string(prediction.tti_ms)+" uncertainty_ms="+std::to_string(prediction.tti_uncertainty_ms)+" class="+attack_classes[prediction.attack_class];
                    input.request(std::move(request));
                }
            } catch(const std::exception& error) {input.disable("VISION_FAULT");events->emit("VISION_FAULT",error.what());detector.reset();tracker.reset();temporal.reset();model.reset_history();}
            catch(...) {input.disable("VISION_FAULT");detector.reset();tracker.reset();temporal.reset();model.reset_history();}
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
    impl_->recorder.discontinuity("CAPTURE_STOPPED");
}
void AutoDodge::discontinuity() {
    {std::lock_guard lock(impl_->mutex);impl_->blocked_generation=impl_->last_generation;impl_->newest.reset();}
    impl_->input.capture_stopped();
    impl_->recorder.discontinuity("CAPTURE_DISCONTINUITY");
}
void AutoDodge::submit(const SmallFrame& frame) {
    {
        std::lock_guard lock(impl_->mutex);
        if(!impl_->accepting||frame.generation<=impl_->blocked_generation||
           (frame.generation==impl_->last_generation&&frame.sequence<=impl_->last_sequence))return;
        if(impl_->newest)++impl_->state.replaced;
        impl_->last_generation=frame.generation;impl_->last_sequence=frame.sequence;impl_->newest=frame;
    }
    impl_->input.frame(frame.source_ms);impl_->recorder.submit(frame);impl_->cv.notify_one();
}
void AutoDodge::disable(){impl_->input.disable("UI_DISABLE");}
MvpConfig AutoDodge::config()const{std::lock_guard lock(impl_->mutex);return impl_->settings;}
void AutoDodge::configure(MvpConfig config) {
    if(!config.roi.valid())return;
    config.direction=std::clamp(config.direction,0,4);config.hold_ms=std::clamp(config.hold_ms,30,90);
    impl_->input.configure(config.direction,config.hold_ms,config.detector_mode?config.temporal.cooldown_ms:config.threat.cooldown_ms);
    {std::lock_guard lock(impl_->mutex);impl_->settings=config;impl_->newest.reset();impl_->reload=true;}
    impl_->cv.notify_one();
    if(!save_config(impl_->config_file,config))impl_->events->emit("CONFIG_SAVE_FAILED","Using in-memory settings");
    impl_->events->emit("CONFIG_CHANGED","ROI/threshold/direction changed; Auto Dodge disabled; F8 to re-arm");
}
MvpSnapshot AutoDodge::snapshot()const{MvpSnapshot s;{std::lock_guard lock(impl_->mutex);s=impl_->state;}s.input=impl_->input.status();s.recording=impl_->recorder.status();return s;}
std::shared_ptr<EventLog> AutoDodge::log()const{return impl_->events;}
std::filesystem::path AutoDodge::config_path()const{return impl_->config_file;}
void AutoDodge::recording(bool enabled){impl_->recorder.enabled(enabled);}
void AutoDodge::mark_sample(const std::string& reason){impl_->recorder.mark(reason);}
}
