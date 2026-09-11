#include <SekiroVisionAI/CombatPipeline.h>
#include <SekiroVisionAI/FramePixels.h>
#include <SekiroVisionAI/Replay.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <locale>
#include <optional>
#include <sstream>
#include <stdexcept>

namespace {
using namespace sekiro;
using namespace sekiro::replay;
using Steady=std::chrono::steady_clock;
struct Options {
    std::filesystem::path video,model,target_model,labels,output{"replay-output"};
    std::string provider{"CPU"};
    bool cv_debug{},allow_provisional{},manual_roi{};
    CombatRoi roi;
    double capture_latency{},input_latency{};
    std::optional<double> deterministic_processing;
};
std::string utf8(const std::filesystem::path& path) {
    const auto bytes=path.u8string();return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};
}
double numeric(const std::string& value) {
    std::istringstream stream(value);stream.imbue(std::locale::classic());double out{};
    if(!(stream>>out)||!std::isfinite(out)||(stream>>std::ws,!stream.eof()))throw std::runtime_error("Invalid numeric option: "+value);
    if(out<0||out>10000)throw std::runtime_error("Replay latency option must be between 0 and 10000 ms");
    return out;
}
void help() {
    std::cout<<"SekiroVisionAI native ReplayHarness (developer evaluation, never sends keys)\n"
       <<"  --video recording.mp4 --model Models/production/model.onnx --out-dir results\n"
       <<"  [--labels reviewed.csv] [--provider CPU|DirectML|CUDA] [--target-model targets.onnx]\n"
       <<"  [--capture-latency-ms N] [--input-latency-ms N]\n"
       <<"Debug only: --cv-debug --allow-provisional-targets --roi l,t,r,b\n"
       <<"Deterministic contract testing only: --deterministic-processing-ms N\n"
       <<"MP4 is decoded by Windows Media Foundation. .svr fixtures are portable.\n";
}
Options options(const std::vector<std::filesystem::path>& args) {
    Options out;
    for(std::size_t i=1;i<args.size();++i) {
        const auto key=utf8(args[i]);
        auto next=[&]()->const std::filesystem::path&{if(++i>=args.size())throw std::runtime_error("Missing value for "+key);return args[i];};
        if(key=="--video")out.video=next();else if(key=="--model")out.model=next();
        else if(key=="--target-model")out.target_model=next();
        else if(key=="--labels")out.labels=next();else if(key=="--out-dir")out.output=next();
        else if(key=="--provider")out.provider=utf8(next());
        else if(key=="--capture-latency-ms")out.capture_latency=numeric(utf8(next()));
        else if(key=="--input-latency-ms")out.input_latency=numeric(utf8(next()));
        else if(key=="--deterministic-processing-ms")out.deterministic_processing=numeric(utf8(next()));
        else if(key=="--cv-debug")out.cv_debug=true;
        else if(key=="--allow-provisional-targets")out.allow_provisional=true;
        else if(key=="--roi") {
            auto text=utf8(next());std::replace(text.begin(),text.end(),',',' ');std::istringstream stream(text);stream.imbue(std::locale::classic());
            if(!(stream>>out.roi.left>>out.roi.top>>out.roi.right>>out.roi.bottom)||(stream>>std::ws,!stream.eof())||!out.roi.valid())
                throw std::runtime_error("Invalid Debug ROI");
            out.manual_roi=true;
        } else throw std::runtime_error("Unknown replay option: "+key);
    }
    if(out.video.empty()||(!out.cv_debug&&out.model.empty()))throw std::runtime_error("--video and --model are required (explicit --cv-debug has no model)");
    if(out.provider!="CPU"&&out.provider!="DirectML"&&out.provider!="CUDA")throw std::runtime_error("Unsupported replay provider");
    return out;
}
void optional_number(std::ostream& stream,bool valid,double value) {
    if(valid&&std::isfinite(value))stream<<value;else stream<<"null";
}
double quantile(std::vector<double> values,double proportion) {
    if(values.empty())return 0;
    std::sort(values.begin(),values.end());
    return values[static_cast<std::size_t>(std::ceil(proportion*static_cast<double>(values.size()-1)))];
}
int run(const std::vector<std::filesystem::path>& args) {
    if(args.size()<2||utf8(args[1])=="--help"){help();return args.size()<2?2:0;}
    const auto opt=options(args);
    std::filesystem::create_directories(opt.output);
    const auto video_hash=sha256_file(opt.video);
    const auto model_hash=opt.model.empty()?std::string{}:sha256_file(opt.model);
    const auto target_path=opt.target_model.empty()?opt.model.parent_path()/"targets.onnx":opt.target_model;
    const auto target_hash=std::filesystem::is_regular_file(target_path)?sha256_file(target_path):std::string{};
    const auto labels=opt.labels.empty()?std::vector<Label>{}:read_labels(opt.labels);
    auto reader=open_video(opt.video);VideoFrame next;
    if(!reader->next(next))throw std::runtime_error("Replay contains no decoded video frames");
    const double first_pts=next.pts_ms,base=1000.0;
    auto source_time=[&](const VideoFrame& frame){return base+frame.pts_ms-first_pts;};
    CombatPipeline pipeline;CombatPipelineConfig config;config.model_path=opt.model;config.target_model_path=opt.target_model;config.provider=opt.provider;
    config.detector_mode=opt.cv_debug?0:1;config.require_semantic_targets=!opt.allow_provisional;
    config.automatic_roi=!opt.manual_roi;config.roi=opt.roi;pipeline.configure(config);
    if(!opt.cv_debug&&!pipeline.model_status().loaded)throw std::runtime_error("Native model load failed: "+pipeline.model_status().reason);
    std::ofstream trace(opt.output/"trace.jsonl");if(!trace)throw std::runtime_error("Cannot write replay trace");
    trace.imbue(std::locale::classic());trace<<std::setprecision(12);
    trace<<"{\"event\":\"REPLAY_BEGIN\",\"contract\":\"svai-shared-native-replay-v1\",\"source_sha256\":"<<json_string(video_hash)
       <<",\"model_sha256\":"<<json_string(model_hash)<<",\"first_decoder_pts_ms\":"<<first_pts
       <<",\"target_model_sha256\":"<<json_string(target_hash)
       <<",\"decoder_clock\":"<<json_string(opt.video.extension()==".svr"?"PRESERVED_SVR_PTS":"WINDOWS_MEDIA_FOUNDATION_PRESENTATION")
       <<",\"clock_origin_ms\":"<<base<<",\"real_os_input\":false,\"capture_latency_ms_assumption\":"<<opt.capture_latency
       <<",\"input_latency_ms_assumption\":"<<opt.input_latency<<",\"provisional_targets_debug\":"<<(opt.allow_provisional?"true":"false")
       <<",\"manual_roi_debug\":"<<(opt.manual_roi?"true":"false")<<",\"processing_clock\":"
       <<json_string(opt.deterministic_processing?"DETERMINISTIC_CONTRACT_FIXTURE":"MEASURED_NATIVE_PROCESSING")<<"}\n";
    CombatContext context{true,true,true,1};DispatchGuard input_guard;
    std::vector<AcceptedDodge> accepted;std::vector<double> processing,age;
    std::uint64_t processed=0,dropped=0,decoded=1,rejected=0,new_temporal_predictions=0,semantic_confirmed_frames=0;
    double available=base,last_pts=0;
    bool have_next=true;
    while(have_next) {
        // Model a one-slot latest-frame mailbox: all decoded frames that arrived
        // while inference was busy replace each other. No future frame enters
        // temporal history before its source PTS + assumed capture availability.
        VideoFrame selected=std::move(next);have_next=reader->next(next);if(have_next)++decoded;
        while(have_next&&source_time(next)+opt.capture_latency<=available) {
            selected=std::move(next);++dropped;have_next=reader->next(next);if(have_next)++decoded;
        }
        if(++processed>1000000)throw std::runtime_error("Replay exceeds one million processed frames; split the source by session");
        SmallFrame frame;frame.color=selected.color;frame.source_width=selected.color->width;frame.source_height=selected.color->height;
        frame.sequence=selected.sequence;frame.generation=selected.generation;frame.source_ms=source_time(selected);
        frame.ready_ms=frame.source_ms+opt.capture_latency;last_pts=selected.pts_ms-first_pts;
        derive_gray(*frame.color,frame);
        const double begin=std::max(frame.ready_ms,available);const auto wall_begin=Steady::now();int calls=0;
        const auto clock=[&] {
            if(opt.deterministic_processing)return begin+(calls++==0?0:*opt.deterministic_processing);
            return begin+std::chrono::duration<double,std::milli>(Steady::now()-wall_begin).count();
        };
        const auto result=pipeline.process(frame,context,clock);available=result.decision_ms;
        if(!opt.cv_debug&&result.new_prediction)++new_temporal_predictions;
        if(result.targets.identity_certain)++semantic_confirmed_frames;
        processing.push_back(result.processing_ms);age.push_back(result.decision_ms-frame.source_ms);
        const auto& p=result.prediction;
        trace<<"{\"event\":\"PREDICTION\",\"sequence\":"<<frame.sequence<<",\"generation\":"<<frame.generation
            <<",\"video_pts_ms\":"<<last_pts<<",\"source_ms\":"<<frame.source_ms<<",\"decision_video_ms\":"<<result.decision_ms-base
            <<",\"source_age_ms\":"<<result.decision_ms-frame.source_ms<<",\"processing_ms\":"<<result.processing_ms
            <<",\"model\":"<<json_string(result.model.version)<<",\"provider\":"<<json_string(result.model.provider)
            <<",\"target_model\":"<<json_string(result.target_model.version)<<",\"target_model_provider\":"<<json_string(result.target_model.provider)
            <<",\"target_model_reason\":"<<json_string(result.target_model.reason)
            <<",\"gameplay_trained\":"<<(p.trained?"true":"false")<<",\"auto_eligible\":"<<(p.auto_eligible?"true":"false")
            <<",\"new_prediction\":"<<(result.new_prediction?"true":"false")<<",\"target_lineage\":"<<result.targets.track_lineage
            <<",\"temporal_history_size\":"<<result.model.history_size<<",\"temporal_length\":"<<result.model.temporal_length
            <<",\"prediction_source_ms\":";optional_number(trace,p.valid,p.source_ms);
        trace<<",\"prediction_sequence\":"<<(p.valid?p.sequence:0)
            <<",\"semantic_targets_confirmed\":"<<(result.targets.identity_certain?"true":"false")
            <<",\"target_reason\":"<<json_string(result.targets.reason)<<",\"roi\":["<<result.roi.left<<','<<result.roi.top<<','<<result.roi.right<<','<<result.roi.bottom<<']'
            <<",\"attack_class\":"<<json_string(p.valid&&p.trained&&p.class_supported?attack_classes[std::clamp(p.attack_class,0,13)]:"UNKNOWN_ATTACK")
            <<",\"attack_state\":"<<json_string(p.valid&&p.trained&&p.state_supported?movement_states[std::clamp(p.state,0,8)]:"UNKNOWN_STATE")
            <<",\"class_supported\":"<<(p.class_supported?"true":"false")<<",\"state_supported\":"<<(p.state_supported?"true":"false")
            <<",\"attack_probability\":";optional_number(trace,p.valid&&p.trained&&p.attack_supported,p.attack_probability);
        trace<<",\"threat_probability\":";optional_number(trace,p.valid&&p.trained&&p.threat_supported,p.threat_probability);
        trace<<",\"tti_ms\":";optional_number(trace,p.valid&&p.trained&&p.tti_supported,p.tti_ms);
        trace<<",\"tti_uncertainty_ms\":";optional_number(trace,p.valid&&p.trained&&p.tti_supported,p.tti_uncertainty_ms);
        trace<<",\"attack_direction\":"<<json_string(p.valid&&p.trained&&p.attack_direction_supported?attack_directions[std::clamp(p.attack_direction,0,7)]:"UNKNOWN")
            <<",\"attack_direction_uncalibrated_softmax\":";
        optional_number(trace,p.valid&&p.trained&&p.attack_direction_supported,p.attack_direction_confidence);
        trace<<",\"dodge_direction\":"<<json_string(dodge_direction_name(result.direction.direction))
            <<",\"direction_geometry_verified\":"<<(result.direction.geometry_verified?"true":"false")
            <<",\"direction_reason\":"<<json_string(result.direction.reason)<<",\"threat_state\":"<<json_string(result.action.decision.state)
            <<",\"reason\":"<<json_string(result.action.decision.reason)<<",\"model_reason\":"<<json_string(p.reason)
            <<",\"request_dodge\":"<<(result.request_dodge?"true":"false")<<"}\n";
        if(result.request_dodge) {
            const double dispatch=result.decision_ms+opt.input_latency;
            const char* rejection=nullptr;
            if(config.detector_mode&&(dispatch<result.action.earliest_send_ms||dispatch>result.action.latest_send_ms))rejection="TTI_WINDOW_EXPIRED_AT_SIMULATED_DISPATCH";
            if(!rejection) {
                DispatchContext guard;guard.enabled=guard.foreground=guard.capture_running=guard.watchdog_ready=true;
                guard.source_ms=frame.source_ms;guard.now_ms=dispatch;guard.cooldown_ms=config.detector_mode?config.temporal.cooldown_ms:config.heuristic.cooldown_ms;
                guard.revision=guard.expected_revision=context.revision;guard.episode=result.action.decision.episode;
                rejection=input_guard.reserve(guard);
            }
            trace<<"{\"event\":"<<json_string(rejection?"SIMULATED_INPUT_REJECTED":"SIMULATED_INPUT_ACCEPTED")
                <<",\"real_os_input\":false,\"sequence\":"<<frame.sequence<<",\"episode\":"<<result.action.decision.episode
                <<",\"video_timestamp_ms\":"<<dispatch-base<<",\"direction\":"<<json_string(dodge_direction_name(result.direction.direction))
                <<",\"reason\":"<<json_string(rejection?rejection:"SHADOW_TOKEN_CONSUMED")<<"}\n";
            if(rejection)++rejected;
            else accepted.push_back({dispatch-base,frame.sequence,result.action.decision.episode,static_cast<int>(result.direction.direction),
                opt.cv_debug?"CV_HEURISTIC":p.valid&&p.trained&&p.class_supported?attack_classes[std::clamp(p.attack_class,0,13)]:"UNKNOWN_ATTACK"});
        }
    }
    if(!trace)throw std::runtime_error("Writing replay trace failed");
    const auto status=pipeline.model_status();
    std::ofstream report(opt.output/"run.json");if(!report)throw std::runtime_error("Cannot write replay run metadata");
    report.imbue(std::locale::classic());report<<std::setprecision(12);
    report<<"{\"contract\":\"svai-shared-native-replay-v1\",\"video\":"<<json_string(utf8(opt.video.filename()))
        <<",\"source_sha256\":"<<json_string(video_hash)<<",\"model_sha256\":"<<json_string(model_hash)
        <<",\"target_model_sha256\":"<<json_string(target_hash)
        <<",\"first_decoder_pts_ms\":"<<first_pts<<",\"annotation_time_origin\":\"FIRST_DECODED_FRAME\""
        <<",\"decoder_clock\":"<<json_string(opt.video.extension()==".svr"?"PRESERVED_SVR_PTS":"WINDOWS_MEDIA_FOUNDATION_PRESENTATION")
        <<",\"model_version\":"<<json_string(status.version)<<",\"provider\":"<<json_string(status.provider)
        <<",\"gameplay_trained\":"<<(status.trained?"true":"false")<<",\"auto_eligible\":"<<(status.auto_eligible?"true":"false")
        <<",\"real_os_input\":false,\"decoded_frames\":"<<decoded<<",\"processed_frames\":"<<processed
        <<",\"new_temporal_predictions\":"<<new_temporal_predictions<<",\"semantic_confirmed_frames\":"<<semantic_confirmed_frames
        <<",\"manual_roi_debug\":"<<(opt.manual_roi?"true":"false")<<",\"provisional_targets_debug\":"<<(opt.allow_provisional?"true":"false")
        <<",\"latest_mailbox_dropped_frames\":"<<dropped<<",\"last_video_pts_ms\":"<<last_pts
        <<",\"simulated_input_accepted\":"<<accepted.size()<<",\"simulated_input_rejected\":"<<rejected
        <<",\"processing_ms_p50\":"<<quantile(processing,0.5)<<",\"processing_ms_p95\":"<<quantile(processing,0.95)
        <<",\"source_age_ms_p95\":"<<quantile(age,0.95)<<",\"capture_latency_ms_assumption\":"<<opt.capture_latency
        <<",\"input_latency_ms_assumption\":"<<opt.input_latency<<",\"measured_capture_or_game_latency\":false"
        <<",\"processing_clock\":"<<json_string(opt.deterministic_processing?"DETERMINISTIC_CONTRACT_FIXTURE":"MEASURED_NATIVE_PROCESSING")
        <<",\"acceptance_labels_available\":"<<(!opt.labels.empty()?"true":"false")<<"}\n";
    if(!report)throw std::runtime_error("Writing run metadata failed");
    if(!opt.labels.empty())write_acceptance(opt.output/"acceptance.json",evaluate(accepted,labels));
    std::cout<<"Replayed "<<processed<<" frames through native CombatPipeline; "<<dropped<<" mailbox replacements; "
             <<accepted.size()<<" simulated input acceptances. No operating-system keys sent.\n";
    if(!status.trained&&!opt.cv_debug)std::cout<<"Model is not gameplay-trained; automatic actions remain ineligible.\n";
    if(opt.labels.empty())std::cout<<"No reviewed acceptance labels supplied; no gameplay success rate is claimed.\n";
    return 0;
}
}
#ifdef _WIN32
int wmain(int argc,wchar_t** argv) {
    try {std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);}
    catch(const std::exception& error){std::cerr<<"Replay failed: "<<error.what()<<'\n';return 1;}
}
#else
int main(int argc,char** argv) {
    try {std::vector<std::filesystem::path> args;for(int i=0;i<argc;++i)args.emplace_back(argv[i]);return run(args);}
    catch(const std::exception& error){std::cerr<<"Replay failed: "<<error.what()<<'\n';return 1;}
}
#endif
