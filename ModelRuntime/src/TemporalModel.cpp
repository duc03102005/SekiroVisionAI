#include <SekiroVisionAI/TemporalModel.h>
#include <onnxruntime_cxx_api.h>
#ifdef SVAI_ORT_DML
#include <dml_provider_factory.h>
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <deque>
#include <fstream>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>

namespace sekiro {
namespace {
double clock_ms() {return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();}
constexpr const char* outputs[]{"attack_probability","threat_probability","tti_ms","tti_uncertainty_ms",
    "state_logits","class_logits","direction_logits","attack_direction_logits"};
constexpr std::int64_t output_sizes[]{1,1,1,1,9,14,5,8};
std::string metadata(const Ort::ModelMetadata& meta,const char* key) {
    Ort::AllocatorWithDefaultOptions allocator;
    const auto value=meta.LookupCustomMetadataMapAllocated(key,allocator);
    return value?std::string(value.get()):std::string{};
}
bool allowed_size(int value) {return value==320||value==384||value==416||value==512;}
bool allowed_length(int value) {return value==8||value==16||value==24||value==32||value==48;}
void validate_results(const std::vector<Ort::Value>& results,std::size_t count) {
    if(results.size()!=count)throw std::runtime_error("Incorrect runtime output count");
    for(std::size_t i=0;i<count;++i) {
        const auto type=results[i].GetTensorTypeAndShapeInfo();
        if(type.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
           type.GetShape()!=std::vector<std::int64_t>{1,output_sizes[i]})
            throw std::runtime_error("Invalid runtime output tensor");
        const auto* values=results[i].GetTensorData<float>();
        for(std::int64_t j=0;j<output_sizes[i];++j)
            if(!std::isfinite(values[j]))throw std::runtime_error("Non-finite model output");
    }
    for(int i=0;i<2;++i) {
        const float value=results[i].GetTensorData<float>()[0];
        if(value<0||value>1)throw std::runtime_error("Model probability outside [0,1]");
    }
    if(results[2].GetTensorData<float>()[0]<0 || results[3].GetTensorData<float>()[0]<0)
        throw std::runtime_error("Negative model timing output");
}
std::string observed_providers(Ort::Session& session) {
    Ort::AllocatorWithDefaultOptions allocator;
    const auto name=session.EndProfilingAllocated(allocator);
    if(!name)return "unavailable (warmup profile missing)";
    const auto path=std::filesystem::path(reinterpret_cast<const char8_t*>(name.get()));
    std::ifstream stream(path,std::ios::binary);
    std::string trace;
    if(stream) {
        stream.seekg(0,std::ios::end);
        const auto size=stream.tellg();
        if(size>0 && size<=16*1024*1024) {
            trace.resize(static_cast<std::size_t>(size));stream.seekg(0);stream.read(trace.data(),size);
        }
        stream.close();
    }
    std::error_code ignored;std::filesystem::remove(path,ignored);
    // The profile is produced by ORT; only extract provider strings, never use
    // graph/session names as paths or instructions. Node placement is observed
    // during warmup, not inferred from the requested execution-provider list.
    const std::regex provider_field(R"regex("provider"\s*:\s*"([^"]+)")regex");
    std::set<std::string> found;
    for(std::sregex_iterator at(trace.begin(),trace.end(),provider_field),end;at!=end;++at) {
        auto provider=(*at)[1].str();
        if(provider=="CPUExecutionProvider")provider="CPU";
        else if(provider=="CUDAExecutionProvider")provider="CUDA";
        else if(provider=="DmlExecutionProvider"||provider=="DMLExecutionProvider")provider="DirectML";
        found.insert(provider);
    }
    if(found.empty())return "unavailable (no profiled runtime nodes)";
    std::string result;
    for(const auto& provider:found){if(!result.empty())result+=" + ";result+=provider;}
    return result;
}
struct ProfileCleanup {
    Ort::Session* session{};
    ~ProfileCleanup() {
        if(!session)return;
        try {
            Ort::AllocatorWithDefaultOptions allocator;
            const auto name=session->EndProfilingAllocated(allocator);
            if(name) {
                std::error_code ignored;
                std::filesystem::remove(std::filesystem::path(reinterpret_cast<const char8_t*>(name.get())),ignored);
            }
        } catch(...) {} // Destruction must not mask the original load failure.
    }
};
}

std::vector<float> model_crop_rgb(const ColorFrame& frame,const CombatRoi& roi,int size) {
    if(!roi.valid() || !allowed_size(size) || frame.width<1 || frame.height<1 || frame.stride<frame.width*4 ||
       frame.bgra.size()<static_cast<std::size_t>(frame.stride)*frame.height)throw std::runtime_error("Invalid color frame/crop");
    const int left=std::clamp(static_cast<int>(std::floor(roi.left*frame.width)),0,frame.width-1);
    const int top=std::clamp(static_cast<int>(std::floor(roi.top*frame.height)),0,frame.height-1);
    const int right=std::clamp(static_cast<int>(std::ceil(roi.right*frame.width)),left+1,frame.width);
    const int bottom=std::clamp(static_cast<int>(std::ceil(roi.bottom*frame.height)),top+1,frame.height);
    std::vector<float> result(static_cast<std::size_t>(3)*size*size);
    for(int y=0;y<size;++y) {
        const double sy=std::clamp((y+0.5)*(bottom-top)/size-0.5,0.0,static_cast<double>(bottom-top-1));
        const int y0=top+static_cast<int>(sy),y1=std::min(y0+1,bottom-1);const double fy=sy-std::floor(sy);
        for(int x=0;x<size;++x) {
            const double sx=std::clamp((x+0.5)*(right-left)/size-0.5,0.0,static_cast<double>(right-left-1));
            const int x0=left+static_cast<int>(sx),x1=std::min(x0+1,right-1);const double fx=sx-std::floor(sx);
            for(int c=0;c<3;++c) {
                const auto at=[&](int a,int b){return frame.bgra[static_cast<std::size_t>(b)*frame.stride+a*4+(2-c)];};
                const double value=(1-fy)*((1-fx)*at(x0,y0)+fx*at(x1,y0))+fy*((1-fx)*at(x0,y1)+fx*at(x1,y1));
                result[(static_cast<std::size_t>(c)*size+y)*size+x]=static_cast<float>(value/255.0);
            }
        }
    }
    return result;
}

struct TemporalModel::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING,"SekiroVisionAI"};
    std::unique_ptr<Ort::Session> session;
    ModelStatus info;
    std::deque<std::vector<float>> history;
    std::vector<float> input;
    std::array<std::int64_t,5> shape{};
    std::uint64_t generation{},sequence{};
    double last_source{},next_sample{};
    std::size_t output_count{7};
    void reset() {history.clear();last_source=next_sample=0;sequence=0;info.history_size=0;}
};

TemporalModel::TemporalModel():impl_(std::make_unique<Impl>()){}
TemporalModel::~TemporalModel()=default;
void TemporalModel::reset_history(){impl_->reset();}
void TemporalModel::load(const std::filesystem::path& path,const std::string& provider) {
    auto& m=*impl_;m.session.reset();m.reset();m.info={};m.generation=0;
    m.info.requested_provider=provider;
    const double load_started=clock_ms();
    if(path.empty()||!std::filesystem::is_regular_file(path)){m.info.reason="NO_MODEL_FILE";return;}
    const bool automatic=provider=="AUTO"||provider=="Auto";
    if(!automatic && provider!="CPU" && provider!="CUDA" && provider!="DirectML") {
        m.info.reason="MODEL_LOAD_FAILED: Unknown execution provider";return;
    }
    std::vector<std::string> candidates;
    if(automatic) {
        const auto available=Ort::GetAvailableProviders();
        if(std::find(available.begin(),available.end(),"CUDAExecutionProvider")!=available.end())candidates.emplace_back("CUDA");
        if(std::find(available.begin(),available.end(),"DmlExecutionProvider")!=available.end() ||
           std::find(available.begin(),available.end(),"DMLExecutionProvider")!=available.end())candidates.emplace_back("DirectML");
    } else if(provider!="CPU")candidates.push_back(provider);
    candidates.emplace_back("CPU");
    std::string failures;
    for(const auto& candidate:candidates)try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(2);options.SetInterOpNumThreads(1);
        options.SetExecutionMode(ORT_SEQUENTIAL);options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
        if(candidate=="CUDA") {
            OrtCUDAProviderOptions cuda{};cuda.device_id=0;options.AppendExecutionProvider_CUDA(cuda);
        } else if(candidate=="DirectML") {
#ifdef SVAI_ORT_DML
            options.DisableMemPattern();
            const OrtDmlApi* dml=nullptr;
            Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML",ORT_API_VERSION,reinterpret_cast<const void**>(&dml)));
            Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML(options,0));
#else
            throw std::runtime_error("DirectML is not included in this build");
#endif
        }
        static std::atomic<std::uint64_t> profile_counter{};
        const auto profile_prefix=std::filesystem::temp_directory_path()/
            ("svai-ort-warmup-"+std::to_string(static_cast<std::uint64_t>(clock_ms()*1000))+"-"+std::to_string(++profile_counter));
        options.EnableProfiling(profile_prefix.c_str());
        auto session=std::make_unique<Ort::Session>(m.env,path.c_str(),options);
        ProfileCleanup profile_cleanup{session.get()};
        const auto meta=session->GetModelMetadata();
        const auto contract=metadata(meta,"svai.contract");
        if((contract!="temporal-v1" && contract!="temporal-v2") ||
            metadata(meta,"svai.preprocess")!="roi-rgb-bilinear-v1")
            throw std::runtime_error("Missing/incompatible model preprocessing metadata");
        const std::size_t output_count=contract=="temporal-v2"?8:7;
        if(session->GetInputCount()!=1||session->GetOutputCount()!=output_count)throw std::runtime_error("Incorrect temporal contract tensor count");
        Ort::AllocatorWithDefaultOptions allocator;
        if(std::string(session->GetInputNameAllocated(0,allocator).get())!="frames")throw std::runtime_error("Expected frames input");
        const auto input_type=session->GetInputTypeInfo(0);
        const auto tensor=input_type.GetTensorTypeAndShapeInfo();const auto dims=tensor.GetShape();
        if(tensor.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || dims.size()!=5 || dims[0]!=1 || dims[2]!=3 ||
            dims[1]<8 || dims[1]>48 || !allowed_length(static_cast<int>(dims[1])) ||
            dims[3]<320 || dims[3]>512 || dims[3]!=dims[4] || !allowed_size(static_cast<int>(dims[3])))
            throw std::runtime_error("Expected float [1,T,3,H,H], T=8/16/24/32/48, H=320/384/416/512");
        std::copy(dims.begin(),dims.end(),m.shape.begin());
        for(std::size_t i=0;i<output_count;++i) {
            if(std::string(session->GetOutputNameAllocated(i,allocator).get())!=outputs[i])throw std::runtime_error("Output names/order mismatch");
            const auto out_type=session->GetOutputTypeInfo(i);const auto out=out_type.GetTensorTypeAndShapeInfo();
            if(out.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || out.GetShape()!=std::vector<std::int64_t>{1,output_sizes[i]})
                throw std::runtime_error("Output type/shape mismatch");
        }
        const double interval=std::stod(metadata(meta,"svai.sample_interval_ms"));
        if(!std::isfinite(interval)||interval<8||interval>100)throw std::runtime_error("Invalid model sampling interval");
        m.info.version=metadata(meta,"svai.model_version");
        if(m.info.version.empty()||m.info.version.size()>160)throw std::runtime_error("Missing model version");
        m.info.trained=metadata(meta,"svai.training_status")=="trained";
        m.info.attack_supported=m.info.trained && metadata(meta,"svai.attack_supported")=="true";
        m.info.auto_eligible=m.info.trained && metadata(meta,"svai.auto_eligible")=="true";
        m.info.threat_supported=m.info.trained && metadata(meta,"svai.threat_supported")=="true";
        m.info.tti_supported=m.info.trained && metadata(meta,"svai.tti_supported")=="true";
        m.info.attack_direction_supported=m.info.trained && contract=="temporal-v2" &&
            metadata(meta,"svai.attack_direction_supported")=="true";
        if(m.info.attack_direction_supported &&
           metadata(meta,"svai.attack_direction_semantics")!="visual_trajectory_screen_with_wolf_reference")
            throw std::runtime_error("Incompatible attack trajectory coordinate semantics");
        m.info.contract=contract;
        m.info.temporal_length=static_cast<int>(dims[1]);m.info.input_size=static_cast<int>(dims[3]);m.info.sample_interval_ms=interval;
        m.input.assign(static_cast<std::size_t>(dims[1]*dims[2]*dims[3]*dims[4]),0.0f);
        auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        auto input=Ort::Value::CreateTensor<float>(memory,m.input.data(),m.input.size(),m.shape.data(),m.shape.size());
        const char* input_names[]{"frames"};const double warmup_started=clock_ms();
        for(int run=0;run<3;++run) {
            const auto results=session->Run(Ort::RunOptions{nullptr},input_names,&input,1,outputs,output_count);
            validate_results(results,output_count);
        }
        m.info.warmup_ms=clock_ms()-warmup_started;m.info.warmup_runs=3;
        m.info.provider=observed_providers(*session);
        profile_cleanup.session=nullptr;
        m.info.provider_fallback=!failures.empty() || (candidate!="CPU" && m.info.provider=="CPU");
        m.info.provider_selection=(automatic?"AUTO available-provider priority CUDA > DirectML > CPU; ":"Requested "+provider+"; ")+
            "selected "+candidate+"; warmup node placement: "+m.info.provider;
        if(!failures.empty())m.info.provider_selection+="; previous attempts: "+failures;
        m.output_count=output_count;m.info.load_ms=clock_ms()-load_started;
        m.info.loaded=true;m.info.reason=m.info.trained?"TEMPORAL_WARMUP":"TEST_MODEL_NO_AUTO_INPUT";m.session=std::move(session);
        return;
    } catch(const std::exception& error) {
        if(!failures.empty())failures+=" | ";
        failures+=candidate+": "+error.what();
    }
    m.info={};m.info.requested_provider=provider;m.info.provider_selection=failures;
    m.info.load_ms=clock_ms()-load_started;m.info.reason="MODEL_LOAD_FAILED: "+failures;
}

ModelPrediction TemporalModel::process(const SmallFrame& frame,const CombatRoi& roi) {
    auto& m=*impl_;ModelPrediction out;
    out.source_ms=frame.source_ms;out.sequence=frame.sequence;out.generation=frame.generation;
    out.version=m.info.version;out.provider=m.info.provider;out.trained=m.info.trained;
    out.attack_supported=m.info.attack_supported;out.auto_eligible=m.info.auto_eligible;
    out.threat_supported=m.info.threat_supported;out.tti_supported=m.info.tti_supported;out.reason=m.info.reason;
    out.attack_direction_supported=m.info.attack_direction_supported;
    if(!m.session)return out;
    if(!frame.color){out.reason="NO_COLOR_MODEL_INPUT";return out;}
    if(!std::isfinite(frame.source_ms)||frame.source_ms<=0){out.reason="INVALID_SOURCE_TIME";return out;}
    if(frame.generation!=m.generation){m.reset();m.generation=frame.generation;}
    if(frame.sequence<=m.sequence || frame.source_ms<=m.last_source){out.reason="OUT_OF_ORDER";return out;}
    if(m.last_source && frame.source_ms-m.last_source>m.info.sample_interval_ms*2.5)m.reset();
    if(m.next_sample && frame.source_ms+0.001<m.next_sample){out.reason="MODEL_SAMPLE_INTERVAL";return out;}
    auto crop=model_crop_rgb(*frame.color,roi,m.info.input_size);
    if(!m.history.empty()) {
        double change=0;std::size_t count=0;
        for(std::size_t i=0;i<crop.size();i+=97){change+=std::abs(crop[i]-m.history.back()[i]);++count;}
        if(change/static_cast<double>(count)>0.45)m.reset(); // Abrupt cut/flash: never combine unrelated scenes.
    }
    m.history.push_back(std::move(crop));m.sequence=frame.sequence;m.last_source=frame.source_ms;
    if(!m.next_sample)m.next_sample=frame.source_ms;
    do {m.next_sample+=m.info.sample_interval_ms;} while(m.next_sample<=frame.source_ms);
    while(m.history.size()>static_cast<std::size_t>(m.info.temporal_length))m.history.pop_front();
    m.info.history_size=static_cast<int>(m.history.size());
    if(m.info.history_size<m.info.temporal_length){out.reason="TEMPORAL_WARMUP";return out;}
    try {
        auto at=m.input.begin();for(const auto& sample:m.history)at=std::copy(sample.begin(),sample.end(),at);
        auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        auto tensor=Ort::Value::CreateTensor<float>(memory,m.input.data(),m.input.size(),m.shape.data(),m.shape.size());
        const char* names[]{"frames"};const double begin=clock_ms();
        const auto results=m.session->Run(Ort::RunOptions{nullptr},names,&tensor,1,outputs,m.output_count);
        out.inference_ms=clock_ms()-begin;
        validate_results(results,m.output_count);
        out.attack_probability=results[0].GetTensorData<float>()[0];out.threat_probability=results[1].GetTensorData<float>()[0];
        out.tti_ms=results[2].GetTensorData<float>()[0];out.tti_uncertainty_ms=results[3].GetTensorData<float>()[0];
        auto best=[&](std::size_t i){const auto* values=results[i].GetTensorData<float>();return static_cast<int>(std::max_element(values,values+output_sizes[i])-values);};
        out.state=best(4);out.attack_class=best(5);out.observed_direction=best(6);
        if(out.attack_direction_supported) {
            out.attack_direction=best(7);
            const float* logits=results[7].GetTensorData<float>();
            const double maximum=logits[out.attack_direction];double sum=0;
            for(std::int64_t i=0;i<output_sizes[7];++i)sum+=std::exp(static_cast<double>(logits[i])-maximum);
            out.attack_direction_confidence=1.0/sum;
        }
        if(out.attack_probability<0||out.attack_probability>1||out.threat_probability<0||out.threat_probability>1||out.tti_ms<0||out.tti_uncertainty_ms<0)
            throw std::runtime_error("Model output outside probability/TTI range");
        out.valid=true;out.reason=out.trained?"MODEL_OUTPUT_UNCALIBRATED":"TEST_MODEL_NO_AUTO_INPUT";m.info.reason=out.reason;
    } catch(const std::exception& e) {m.info.reason=std::string("MODEL_RUNTIME_FAILED: ")+e.what();out.reason=m.info.reason;m.session.reset();m.info.loaded=false;m.reset();}
    return out;
}
ModelStatus TemporalModel::status()const{return impl_->info;}
}
