#include <SekiroVisionAI/CombatPipeline.h>
#include <SekiroVisionAI/FramePixels.h>
#include <SekiroVisionAI/Replay.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <type_traits>

using namespace sekiro;
using namespace sekiro::replay;
namespace {
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class F> void rejects(F&& operation,const char* reason) {
    bool rejected=false;try{operation();}catch(const std::exception&){rejected=true;}require(rejected,reason);
}
struct Temporary {
    std::filesystem::path path;
    Temporary() {
        path=std::filesystem::temp_directory_path()/ ("svai-replay-contract-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(path);
    }
    ~Temporary(){std::error_code error;std::filesystem::remove_all(path,error);}
};
template<class T> void integer(std::ostream& stream,T value) {
    auto bits=static_cast<std::make_unsigned_t<T>>(value);
    for(std::size_t i=0;i<sizeof(T);++i)stream.put(static_cast<char>((bits>>(i*8))&255));
}
void raw_video(const std::filesystem::path& path,bool duplicate=false) {
    std::ofstream stream(path,std::ios::binary);stream.write("SVRRAW01",8);integer<std::uint32_t>(stream,32);integer<std::uint32_t>(stream,32);
    for(std::uint64_t index=0;index<3;++index) {
        integer<std::int64_t>(stream,static_cast<std::int64_t>((duplicate&&index==2?1:index)*333333));
        integer<std::uint64_t>(stream,1);integer<std::uint64_t>(stream,index+1);
        for(int pixel=0;pixel<32*32;++pixel){stream.put(char(index*20));stream.put(char(30));stream.put(char(200));stream.put(char(255));}
    }
}
void raw_and_hash(const Temporary& temp) {
    const auto path=temp.path/"source.svr";raw_video(path);auto reader=open_raw_video(path);VideoFrame frame;
    for(int i=0;i<3;++i) {
        require(reader->next(frame),"Lossless fixture frame missing");require(frame.sequence==std::uint64_t(i+1)&&frame.generation==1,"Raw source identity changed");
        require(std::abs(frame.pts_ms-i*33.3333)<1e-6,"Raw PTS changed");require(frame.color->bgra[0]==i*20&&frame.color->bgra[2]==200,"Raw BGRA changed");
    }
    require(!reader->next(frame),"Raw EOF not reported");
    raw_video(temp.path/"duplicate.svr",true);auto bad=open_raw_video(temp.path/"duplicate.svr");bad->next(frame);bad->next(frame);
    rejects([&]{bad->next(frame);},"Duplicate PTS must be rejected, never relabeled fresh");
    raw_video(temp.path/"truncated.svr");std::filesystem::resize_file(temp.path/"truncated.svr",30);
    rejects([&]{auto truncated=open_raw_video(temp.path/"truncated.svr");truncated->next(frame);},"Truncated video was accepted");
    {std::ofstream stream(temp.path/"hash.bin",std::ios::binary);}
    require(sha256_file(temp.path/"hash.bin")=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","SHA-256 empty vector failed");
    {std::ofstream stream(temp.path/"hash.bin",std::ios::binary);stream<<"abc";}
    require(sha256_file(temp.path/"hash.bin")=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA-256 abc vector failed");
    {std::ofstream stream(temp.path/"hash.bin",std::ios::binary);stream<<std::string(1000000,'a');}
    require(sha256_file(temp.path/"hash.bin")=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","SHA-256 multi-block vector failed");
}
Label threat(std::string id,double start,double end,double impact) {
    return {std::move(id),"THREAT","HORIZONTAL_SLASH","OBSERVED_CONTACT",start,end,impact,impact,70,130,true};
}
void acceptance(const Temporary& temp) {
    std::vector<Label> labels{threat("success",100,500,400),threat("early",600,1000,900),
        threat("late",1100,1500,1400),threat("missed",1600,2000,1900),
        {"idle","NON_THREAT","","REVIEWED_VISUAL",2100,62100,0,0,0,0,true},
        {"no-hit","THREAT","THRUST","CONTACT_CENSORED",67000,68000,0,0,0,0,true}};
    std::vector<AcceptedDodge> actions;
    for(double time:{300.,350.,650.,1450.,2200.,65500.,67500.})actions.push_back({time,0,0,1,"HORIZONTAL_SLASH"});
    const auto result=evaluate(actions,labels);
    require(result.success==1&&result.early==1&&result.late==1&&result.missed==1&&result.false_dodge==2,"Five-way acceptance classification failed");
    require(result.unevaluated==2&&result.excluded==1,"Unknown/censored evidence must not become a fabricated negative or missed contact");
    require(result.negative_minutes==1&&result.false_in_negative==1,"False Dodge denominator must use only reviewed non-threat time");
    write_acceptance(temp.path/"acceptance.json",result);
    const std::vector<Label> combo{threat("combo1",100,500,400),threat("combo2",100,500,430)};
    require(evaluate({{300,0,1,1,"THRUST"},{330,0,2,1,"THRUST"}},combo).success==2,"Overlapping combo windows require one-to-one matching");
    auto invalid=labels;invalid[0].impact_max_ms=600;
    rejects([&]{evaluate(actions,invalid);},"Contact outside reviewed interval must be rejected");
    auto duplicate=labels;duplicate.push_back(labels[0]);
    rejects([&]{evaluate(actions,duplicate);},"Duplicate attack event IDs must be rejected");
    rejects([&]{evaluate({{400,0,0,1,""},{300,0,0,1,""}},labels);},"Unordered action timestamps must be rejected");
    {std::ofstream csv(temp.path/"labels.csv");csv<<"event_id,kind,start_ms,end_ms,impact_min_ms,impact_max_ms,lead_min_ms,lead_max_ms,attack_class,evidence,reviewed\n"
        <<"one,THREAT,100,500,400,400,70,130,HORIZONTAL_SLASH,OBSERVED_CONTACT,true\n"
        <<"censored,THREAT,600,1000,,,,,THRUST,CONTACT_CENSORED,true\n";}
    const auto parsed=read_labels(temp.path/"labels.csv");require(parsed.size()==2&&evaluate({},parsed).excluded==1,"CSV evidence/masks changed");
}
ModelPrediction prediction(double source,double probability=0.95) {
    ModelPrediction out;out.valid=out.trained=out.attack_supported=out.threat_supported=out.tti_supported=out.auto_eligible=true;
    out.source_ms=source;out.attack_probability=out.threat_probability=probability;out.tti_ms=110;out.tti_uncertainty_ms=5;out.attack_class=0;
    out.class_supported=true;return out;
}
void policy_and_direction() {
    TemporalPolicy policy;TemporalDecision engine;
    for(int i=0;i<8;++i)engine.step(prediction(1000+i*33,0.01),1010+i*33,policy);
    engine.step(prediction(1300),1310,policy);require(engine.step(prediction(1333),1343,policy).decision.trigger,"Contract attack should reserve one action");
    engine.cancel_candidate();
    for(int i=0;i<50;++i)require(!engine.step(prediction(1366+i*33),1376+i*33,policy).decision.trigger,"Focus/identity cancellation must retain consumed strike after cooldown");
    TargetState targets;auto p=prediction(1000);
    require(!choose_dodge_direction(targets,p,false).valid,"Unknown escape geometry must abstain in strict mode");
    const auto provisional=choose_dodge_direction(targets,p,true);
    require(provisional.valid&&!provisional.geometry_verified&&provisional.confidence==0,"Provisional side direction must remain explicitly unverified");
    targets.escape.right={true,0.8,0.9};const auto chosen=choose_dodge_direction(targets,p,false);
    require(chosen.valid&&chosen.geometry_verified&&chosen.direction==DodgeDirection::Right,"Shared chooser must use verified escape evidence");
    p.attack_class=4;require(!choose_dodge_direction(targets,p,true).valid,"Sweep must not select a spurious safe side");
    p.class_supported=false;
    require(choose_dodge_direction(targets,p,false).valid,"Unsupported class logits cannot control the shared direction policy");
}
SmallFrame color_frame(double source,std::uint64_t sequence) {
    auto color=std::make_shared<ColorFrame>();color->width=64;color->height=48;color->stride=256;color->bgra.resize(64*48*4);
    for(int y=0;y<48;++y)for(int x=0;x<64;++x) {
        auto* at=color->bgra.data()+(y*64+x)*4;at[0]=std::uint8_t(x*3);at[1]=std::uint8_t(y*4);at[2]=120;at[3]=255;
    }
    SmallFrame frame;frame.color=color;frame.source_width=64;frame.source_height=48;frame.source_ms=source;frame.ready_ms=source+1;
    frame.sequence=sequence;frame.generation=1;derive_gray(*color,frame);return frame;
}
void shared_model(const std::filesystem::path& fixture) {
    CombatPipeline pipeline;CombatPipelineConfig config;config.model_path=fixture;config.provider="CPU";
    config.automatic_roi=false;config.require_semantic_targets=false;pipeline.configure(config);
    require(pipeline.model_status().loaded,"Shared pipeline cannot load ONNX fixture");
    require(!pipeline.model_status().trained&&!pipeline.model_status().auto_eligible,"Synthetic model must never be gameplay eligible");
    TemporalModel reference;reference.load(fixture,"CPU");CombatContext context{true,true,true,1};bool valid_seen=false;
    for(std::uint64_t i=0;i<24;++i) {
        const auto frame=color_frame(1000+static_cast<double>(i)*35,i+1);
        const auto actual=pipeline.process(frame,context,[&]{return frame.source_ms+10;});
        const auto expected=reference.process(frame,config.roi);
        require(!actual.request_dodge,"Synthetic model reached automatic input request");
        require(actual.model.history_size==reference.status().history_size,"Production/replay temporal history differs from native model");
        require(actual.prediction.valid==expected.valid,"Production/replay model validity differs");
        if(expected.valid) {
            valid_seen=true;require(std::abs(actual.prediction.attack_probability-expected.attack_probability)<1e-6,"Shared model/preprocess output differs");
            require(actual.prediction.sequence==expected.sequence&&actual.prediction.source_ms==expected.source_ms,"Prediction source identity changed");
        }
    }
    require(valid_seen,"Native replay never reached actual ONNX execution");
    auto frame=color_frame(1900,30);context.auto_enabled=false;
    auto result=pipeline.process(frame,context,[&]{return frame.source_ms+10;});
    require(!result.request_dodge&&std::string(result.action.decision.reason)=="DISABLED","Disabled replay/live context must cancel request");
    frame=color_frame(1935,31);context.auto_enabled=true;context.foreground=false;
    result=pipeline.process(frame,context,[&]{return frame.source_ms+10;});
    require(!result.request_dodge&&std::string(result.action.decision.reason)=="LOST_FOCUS","Focus loss must cancel request");
    frame=color_frame(1970,32);context.foreground=true;
    result=pipeline.process(frame,context,[&]{return frame.source_ms+3001;});
    require(!result.request_dodge&&std::string(result.action.decision.reason)=="STALE_FRAME","3000 ms stale input must remain stale");
    result=pipeline.process(frame,context,[&]{return frame.source_ms+10;});
    require(!result.request_dodge&&std::string(result.action.decision.reason)=="OUT_OF_ORDER","Duplicate source must not advance dwell");

    // Fixed-crop diagnostics must still execute the temporal model when an
    // unrelated automatic assignment changes. Actions keep identity guards.
    pipeline.configure(config);reference.load(fixture,"CPU");std::uint64_t prior_lineage=0;int changes=0,inferences=0;
    for(std::uint64_t i=0;i<32;++i) {
        const auto fixed=color_frame(3000+static_cast<double>(i)*35,i+1);
        const std::array<TargetDetection,2> detections{{
            {TargetRole::Enemy,{0.32,0.20,0.52,0.55},0.95,fixed.source_ms,fixed.sequence,1,i/3+1,true},
            {TargetRole::Wolf,{0.55,0.58,0.72,0.86},0.95,fixed.source_ms,fixed.sequence,1,999,true}}};
        const auto observed=pipeline.process(fixed,context,[&]{return fixed.source_ms+10;},detections);
        const auto expected=reference.process(fixed,config.roi);
        if(prior_lineage&&observed.targets.track_lineage&&prior_lineage!=observed.targets.track_lineage)++changes;
        if(observed.targets.track_lineage)prior_lineage=observed.targets.track_lineage;
        require(observed.model.history_size==reference.status().history_size,"Automatic target churn erased fixed-crop diagnostic history");
        require(observed.prediction.valid==expected.valid,"Fixed-crop diagnostic model execution diverged under target churn");
        if(observed.new_prediction)++inferences;
        require(!observed.request_dodge,"Target churn or synthetic diagnostic model must not authorize an action");
    }
    require(changes>=2&&inferences>0,"Target churn regression did not exercise association changes and actual model inference");
}
#ifdef _WIN32
void native_mp4(const std::filesystem::path& path) {
    auto reader=open_video(path);VideoFrame frame;int count=0;double previous=-1,first=0;
    std::vector<double> timestamps;
    while(reader->next(frame)) {
        require(frame.color->width==320&&frame.color->height==192,"Media Foundation decoded wrong dimensions");
        require(frame.pts_ms>previous,"Native MP4 PTS must be increasing");previous=frame.pts_ms;
        if(count==0)first=frame.pts_ms;
        timestamps.push_back(frame.pts_ms);
        const std::array<std::array<int,3>,4> colors{{{0,0,255},{0,255,0},{255,0,0},{255,255,255}}};
        for(int quadrant=0;quadrant<4;++quadrant) {
            const int x=quadrant%2?240:80,y=quadrant/2?144:48;
            const auto* pixel=frame.color->bgra.data()+y*frame.color->stride+x*4;
            for(int channel=0;channel<3;++channel)require(std::abs(int(pixel[channel])-colors[quadrant][channel])<=12,"Native H264 decoding changed color channel or vertical orientation");
            require(pixel[3]==255,"Decoded RGB32 must normalize alpha to opaque");
        }
        int decoded_index=0;
        for(int bit=0;bit<5;++bit) {
            const auto* pixel=frame.color->bgra.data()+16*frame.color->stride+(16+bit*16)*4;
            if(pixel[0]>127&&pixel[1]>127&&pixel[2]>127)decoded_index|=1<<bit;
        }
        if(decoded_index!=count)throw std::runtime_error("Native H264 decoded wrong original-frame identity: expected="+
            std::to_string(count)+" observed="+std::to_string(decoded_index)+" pts_ms="+std::to_string(frame.pts_ms));
        ++count;
    }
    std::ostringstream detail;
    detail<<"Native H264 decoded_frames="<<count<<" first_pts_ms="<<first<<" last_pts_ms="<<previous
          <<" span_ms="<<previous-first<<" source_sha256="<<sha256_file(path)<<" pts_ms=[";
    for(std::size_t i=0;i<timestamps.size();++i){if(i)detail<<',';detail<<timestamps[i];}detail<<']';
    std::cout<<detail.str()<<'\n';
    require(count==30,"Native H264 lost or duplicated original frames");
    // Source Reader's presentation clock need not begin at zero (the Windows
    // H.264 fixture was observed to start at 133.333 ms with all 30 frames).
    // Replay uses the same explicit first-PTS origin. Do not rewrite the native
    // decoder timestamps; verify every interval and original image identity.
    for(std::size_t i=0;i<timestamps.size();++i)
        if(std::abs((timestamps[i]-first)-static_cast<double>(i)*1000/30)>=0.2)
            throw std::runtime_error("Native H264 source PTS interval changed: "+detail.str());
}
#endif
}
int main(int argc,char** argv) {
    try {
        Temporary temp;raw_and_hash(temp);acceptance(temp);policy_and_direction();
        if(argc>1)shared_model(std::filesystem::path(argv[1]));
        else throw std::runtime_error("Native replay test requires the synthetic ONNX fixture path");
#ifdef _WIN32
        if(argc>2)native_mp4(std::filesystem::path(argv[2]));
        else throw std::runtime_error("Windows replay test requires generated synthetic H264 MP4 fixture");
#endif
        std::cout<<"Native replay/production shared model, history, cancellation, direction, SHA-256, PTS and acceptance contracts passed. Synthetic fixtures; no game or OS input.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
