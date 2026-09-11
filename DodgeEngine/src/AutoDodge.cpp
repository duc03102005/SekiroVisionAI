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
    value.resize(length);return std::filesystem::path(value).parent_path()/L"Models"/L"production"/L"model.onnx";
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
    c.direction=std::clamp(get(L"input",L"direction",-1),-1,4);c.hold_ms=std::clamp(get(L"input",L"hold_ms",45),30,90);
    // Debug source/model/provider/ROI selection is session-local. A portable
    // application may move between launches, so never restore its old absolute
    // bundled path or silently resume a previously selected heuristic detector.
    c.automatic_roi=true;
    c.preset=std::clamp(get(L"detector",L"preset",1),0,2);
    c.detector_mode=1;
    c.model_path=bundled_model_path();
    c.provider="Auto";
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
    put(L"meta",L"schema_version",3);
    put(L"roi",L"automatic",1);
    put(L"roi",L"left_per_mille",static_cast<int>(std::lround(c.roi.left*1000)));put(L"roi",L"top_per_mille",static_cast<int>(std::lround(c.roi.top*1000)));
    put(L"roi",L"right_per_mille",static_cast<int>(std::lround(c.roi.right*1000)));put(L"roi",L"bottom_per_mille",static_cast<int>(std::lround(c.roi.bottom*1000)));
    put(L"detector",L"enter_percent",static_cast<int>(std::lround(c.threat.enter_score*100)));
    put(L"detector",L"exit_percent",static_cast<int>(std::lround(c.threat.exit_score*100)));
    put(L"detector",L"confidence_percent",static_cast<int>(std::lround(c.threat.min_confidence*100)));
    put(L"detector",L"arming_ms",static_cast<int>(c.threat.dwell_ms));put(L"detector",L"cooldown_ms",static_cast<int>(c.threat.cooldown_ms));
    put(L"detector",L"quiet_ms",static_cast<int>(c.threat.quiet_ms));put(L"detector",L"preset",c.preset);
    put(L"input",L"direction",c.direction);put(L"input",L"hold_ms",c.hold_ms);
    put(L"model",L"mode",1);put(L"model",L"provider",3);
    // Delete legacy/debug absolute paths. Recompute the managed path from the
    // running executable on every launch while preserving input/threshold prefs.
    if(!WritePrivateProfileStringW(L"model",L"path",nullptr,path.c_str()))ok=false;
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
    std::uint64_t settings_revision{1};
    MvpSnapshot state;
    WindowTarget selected_target;
    std::filesystem::path config_file=mvp_data_directory()/L"application.ini";
    MvpConfig settings=read_config(config_file);
    std::thread worker;

    Impl() {
        input.configure(std::max(0,settings.direction),settings.hold_ms,settings.detector_mode?settings.temporal.cooldown_ms:settings.threat.cooldown_ms);
        input.detector_ready(settings.detector_mode==0,"MODEL_STARTING");
        if(!save_config(config_file,settings))events->emit("CONFIG_SAVE_FAILED","Using in-memory settings");
        events->emit("APPLICATION_START","Native shared combat pipeline; model availability reported separately; F8 toggle/F9 release; build " SVAI_BUILD_REVISION);
        worker=std::thread([this]{run();});
    }
    ~Impl() {
        input.capture_stopped();
        {std::lock_guard lock(mutex);quit=true;accepting=false;newest.reset();}
        cv.notify_one(); if(worker.joinable())worker.join();
    }
    void run() noexcept {
        CombatPipeline pipeline;
        double last_log=0;
        std::string last_state;
        while(true) {
            SmallFrame frame;MvpConfig cfg;WindowTarget target;bool load=false,has_frame=false;std::uint64_t cfg_revision{};
            {std::unique_lock lock(mutex);cv.wait(lock,[&]{return quit||reload||newest.has_value();});if(quit)break;
             cfg=settings;cfg_revision=settings_revision;target=selected_target;load=std::exchange(reload,false);
             if(newest){frame=std::move(*newest);newest.reset();has_frame=true;}}
            try {
                if(load) {
                    CombatPipelineConfig config;config.roi=cfg.roi;config.automatic_roi=cfg.automatic_roi;
                    config.require_semantic_targets=cfg.detector_mode!=0;
                    config.detector_mode=cfg.detector_mode;config.direction_override=cfg.direction;
                    config.heuristic=cfg.threat;config.temporal=cfg.temporal;
                    config.model_path=cfg.model_path;config.provider=cfg.provider;
                    pipeline.configure(config);last_state.clear();
                    const auto info=pipeline.model_status();
                    {std::lock_guard lock(mutex);if(cfg_revision!=settings_revision)continue;
                     input.detector_ready(cfg.detector_mode==0||supports_auto(info),model_arm_reason(info));state.model=info;}
                    events->emit(info.loaded?"MODEL_LOADED":"MODEL_UNAVAILABLE",info.version+" provider="+info.provider+" "+info.reason);
                }
                if(!has_frame)continue;
                const auto input_state=input.status();
                const bool foreground=target.hwnd&&GetForegroundWindow()==target.hwnd&&target_is_current(target);
                CombatContext context{input_state.enabled,foreground,input_state.capture_running,input_state.revision};
                auto result=pipeline.process(frame,context,[]{return qpc_ms();});
                const auto& prediction=result.prediction;
                const auto& motion=result.motion;
                const auto& threat=result.action.decision;
                const double now=result.decision_ms;
                {
                    std::lock_guard lock(mutex);
                    if(cfg_revision!=settings_revision||!accepting||frame.generation<=blocked_generation)continue;
                    input.detector_ready(cfg.detector_mode==0||supports_auto(result.model),model_arm_reason(result.model));
                    if(input.status().revision!=input_state.revision)continue;
                    state.preview=frame;state.has_frame=true;state.motion=motion;state.model=result.model;
                    state.combat_roi=result.roi;state.target_status=result.targets.reason;
                    // Sampling skips do not overwrite a supported prediction with an empty one.
                    if(result.new_prediction||!cfg.detector_mode){state.prediction=prediction;state.threat=threat;}
                    if(result.request_dodge)state.direction_status=std::string(dodge_direction_name(result.direction.direction))+" / "+result.direction.reason;
                    else if(result.new_prediction||!cfg.detector_mode)state.direction_status=result.direction.reason;
                    ++state.processed;state.cpu_ms=result.processing_ms;state.frame_age_ms=age_ms(frame.source_ms,now);
                    if(result.request_dodge)++state.threats;
                }
                if(last_state!=threat.state) {
                    events->emit("THREAT_STATE",std::string(threat.state)+" reason="+threat.reason+" frame="+std::to_string(frame.sequence),threat.episode);
                    last_state=threat.state;
                }
                if(now-last_log>=200||result.request_dodge) {
                    std::ostringstream values;values.imbue(std::locale::classic());values<<std::fixed<<std::setprecision(3)
                        <<"frame="<<frame.sequence<<" source_ms="<<frame.source_ms<<" decision_ms="<<now
                        <<" source_age_ms="<<age_ms(frame.source_ms,now)<<" pipeline_ms="<<result.processing_ms
                        <<" target="<<result.targets.reason<<" lineage="<<result.targets.track_lineage
                        <<" semantic_identity="<<result.targets.identity_certain<<" reason="<<threat.reason;
                    if(cfg.detector_mode)values<<" model="<<prediction.version<<" provider="<<result.model.provider
                        <<" attack_p="<<(prediction.valid&&prediction.trained&&prediction.attack_supported?std::to_string(prediction.attack_probability):"null")
                        <<" threat_p="<<(prediction.valid&&prediction.trained&&prediction.threat_supported?std::to_string(prediction.threat_probability):"null")
                        <<" tti_ms="<<(prediction.valid&&prediction.tti_supported?std::to_string(prediction.tti_ms):"null")
                        <<" uncertainty_ms="<<(prediction.valid&&prediction.tti_supported?std::to_string(prediction.tti_uncertainty_ms):"null")
                        <<" attack_class="<<attack_classes[prediction.attack_class]<<" inference_ms="<<prediction.inference_ms;
                    else values<<" cv_score="<<motion.score<<" cv_quality="<<motion.confidence<<" flow="<<motion.flow
                        <<" acceleration="<<motion.acceleration<<" camera="<<motion.camera_dx<<","<<motion.camera_dy;
                    values<<" direction="<<dodge_direction_name(result.direction.direction)<<" direction_reason="<<result.direction.reason
                        <<" direction_geometry_verified="<<result.direction.geometry_verified;
                    events->emit(result.request_dodge?"THREAT_READY":cfg.detector_mode?"MODEL_SIGNALS":"CV_SIGNALS",values.str(),threat.episode);last_log=now;
                }
                if(result.request_dodge) {
                    DodgeRequest request;request.revision=input_state.revision;request.episode=threat.episode;
                    request.source_ms=frame.source_ms;request.sequence=frame.sequence;
                    request.direction=static_cast<int>(result.direction.direction);
                    request.earliest_send_ms=result.action.earliest_send_ms;request.latest_send_ms=result.action.latest_send_ms;
                    request.model_version=cfg.detector_mode?prediction.version:"CV_HEURISTIC_DEBUG";
                    request.prediction_details="class="+std::string(attack_classes[prediction.attack_class])+" tti_ms="+
                        (prediction.tti_supported?std::to_string(prediction.tti_ms):"null")+" direction_reason="+result.direction.reason;
                    input.request(std::move(request));
                    if(recorder.status().enabled)recorder.mark("PREDICTED_DODGE",threat.episode);
                }
            } catch(const std::exception& error) {input.disable("VISION_FAULT");events->emit("VISION_FAULT",error.what());pipeline.cancel_pending();}
            catch(...) {input.disable("VISION_FAULT");pipeline.cancel_pending();}
        }
    }
};

AutoDodge::AutoDodge():impl_(std::make_unique<Impl>()){}
AutoDodge::~AutoDodge()=default;
void AutoDodge::start(const WindowTarget& target) {
    impl_->input.target(target);
    {std::lock_guard lock(impl_->mutex);impl_->selected_target=target;impl_->newest.reset();impl_->state={};impl_->last_sequence=impl_->last_generation=impl_->blocked_generation=0;impl_->accepting=true;}
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
        // Stop cannot interleave with this last accepted frame and resurrect capture readiness.
        impl_->input.frame(frame.source_ms);
    }
    impl_->recorder.submit(frame);impl_->cv.notify_one();
}
void AutoDodge::disable(){impl_->input.disable("UI_DISABLE");}
void AutoDodge::toggle(){impl_->input.toggle();}
MvpConfig AutoDodge::config()const{std::lock_guard lock(impl_->mutex);return impl_->settings;}
void AutoDodge::configure(MvpConfig config) {
    if(!config.roi.valid())return;
    config.direction=std::clamp(config.direction,-1,4);config.hold_ms=std::clamp(config.hold_ms,30,90);
    {std::lock_guard lock(impl_->mutex);
     impl_->input.configure(std::max(0,config.direction),config.hold_ms,config.detector_mode?config.temporal.cooldown_ms:config.threat.cooldown_ms);
     impl_->input.detector_ready(false,"MODEL_LOADING");
     impl_->settings=config;++impl_->settings_revision;impl_->newest.reset();impl_->reload=true;}
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
