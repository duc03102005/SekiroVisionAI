#include <SekiroVisionAI/TargetDetector.h>
#include <onnxruntime_cxx_api.h>
#ifdef SVAI_ORT_DML
#include <dml_provider_factory.h>
#endif
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>

namespace sekiro {
namespace {
double clock_ms() {
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
std::string metadata(const Ort::ModelMetadata& meta,const char* key) {
    Ort::AllocatorWithDefaultOptions allocator;
    const auto value=meta.LookupCustomMetadataMapAllocated(key,allocator);
    return value?std::string(value.get()):std::string{};
}
constexpr const char* input_names[]{"frame"};
constexpr const char* output_names[]{"scores","boxes"};

void validate_outputs(const std::vector<Ort::Value>& output,int height,int width) {
    if(output.size()!=2) throw std::runtime_error("Target output count mismatch");
    const std::array<std::vector<std::int64_t>,2> shapes{
        std::vector<std::int64_t>{1,2,height,width}, std::vector<std::int64_t>{1,2,4,height,width}};
    for(std::size_t i=0;i<2;++i) {
        if(!output[i].IsTensor()) throw std::runtime_error("Target output is not a tensor");
        const auto type=output[i].GetTensorTypeAndShapeInfo();
        if(type.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || type.GetShape()!=shapes[i])
            throw std::runtime_error("Target output shape/type mismatch");
        const float* values=output[i].GetTensorData<float>();
        for(std::size_t j=0;j<type.GetElementCount();++j)
            if(!std::isfinite(values[j]) || values[j]<0 || values[j]>1)
                throw std::runtime_error("Target output is nonfinite or outside normalized range");
    }
}

double iou(const TargetBox& a,const TargetBox& b) {
    const double intersection=std::max(0.0,std::min(a.right,b.right)-std::max(a.left,b.left))*
        std::max(0.0,std::min(a.bottom,b.bottom)-std::max(a.top,b.top));
    return intersection/std::max(1e-9,(a.right-a.left)*(a.bottom-a.top)+(b.right-b.left)*(b.bottom-b.top)-intersection);
}

std::string profiled_provider(Ort::Session& session) {
    Ort::AllocatorWithDefaultOptions allocator;
    const auto name=session.EndProfilingAllocated(allocator);
    if(!name) return "unavailable (no target profile)";
    const std::string profile_name=name.get();
    const std::filesystem::path path(std::u8string(profile_name.begin(),profile_name.end()));
    std::ifstream input(path,std::ios::binary);
    std::string trace;
    if(input) {
        input.seekg(0,std::ios::end); const auto length=input.tellg();
        if(length>0 && length<=16*1024*1024) {
            trace.resize(static_cast<std::size_t>(length)); input.seekg(0); input.read(trace.data(),length);
        }
        input.close();
    }
    std::error_code ignored; std::filesystem::remove(path,ignored);
    const std::regex pattern(R"regex("provider"\s*:\s*"([^"]+)")regex");
    std::set<std::string> providers;
    for(std::sregex_iterator at(trace.begin(),trace.end(),pattern),end;at!=end;++at) {
        auto provider=(*at)[1].str();
        if(provider=="CPUExecutionProvider") provider="CPU";
        else if(provider=="CUDAExecutionProvider") provider="CUDA";
        else if(provider=="DmlExecutionProvider" || provider=="DMLExecutionProvider") provider="DirectML";
        providers.insert(provider);
    }
    std::string result;
    for(const auto& provider:providers) {if(!result.empty()) result+=" + "; result+=provider;}
    return result.empty()?"unavailable (no profiled target nodes)":result;
}
}

std::vector<float> target_frame_rgb(const ColorFrame& frame,int width,int height) {
    if(frame.width<1 || frame.height<1 || frame.width>8192 || frame.height>8192 ||
        frame.stride<frame.width*4 || frame.bgra.size()<static_cast<std::size_t>(frame.stride)*frame.height ||
        width<64 || width>640 || height<64 || height>640)
        throw std::runtime_error("Invalid target color frame or input shape");
    std::vector<float> result(static_cast<std::size_t>(3)*width*height);
    for(int y=0;y<height;++y) {
        const double sy=std::clamp((y+0.5)*frame.height/height-0.5,0.0,static_cast<double>(frame.height-1));
        const int y0=static_cast<int>(sy),y1=std::min(y0+1,frame.height-1); const double fy=sy-y0;
        for(int x=0;x<width;++x) {
            const double sx=std::clamp((x+0.5)*frame.width/width-0.5,0.0,static_cast<double>(frame.width-1));
            const int x0=static_cast<int>(sx),x1=std::min(x0+1,frame.width-1); const double fx=sx-x0;
            for(int c=0;c<3;++c) {
                const auto value=[&](int a,int b){return frame.bgra[static_cast<std::size_t>(b)*frame.stride+a*4+2-c];};
                const double interpolated=(1-fy)*((1-fx)*value(x0,y0)+fx*value(x1,y0))+
                    fy*((1-fx)*value(x0,y1)+fx*value(x1,y1));
                result[(static_cast<std::size_t>(c)*height+y)*width+x]=static_cast<float>(interpolated/255.0);
            }
        }
    }
    return result;
}

struct TargetDetector::Impl {
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING,"SekiroVisionAI-Targets"};
    std::unique_ptr<Ort::Session> session;
    TargetModelStatus info;
    std::array<std::int64_t,4> shape{};
    std::vector<float> input;
    int grid_height{},grid_width{};
    double threshold{0.75},last_source{};
    std::uint64_t last_sequence{},generation{};
};

TargetDetector::TargetDetector():impl_(std::make_unique<Impl>()){}
TargetDetector::~TargetDetector()=default;
TargetModelStatus TargetDetector::status() const {return impl_->info;}

void TargetDetector::load(const std::filesystem::path& path,const std::string& provider) {
    auto& m=*impl_; m.session.reset(); m.info={}; m.info.requested_provider=provider;
    m.last_source=0; m.last_sequence=m.generation=0;
    if(path.empty() || !std::filesystem::is_regular_file(path)) {m.info.reason="NO_TARGET_MODEL";return;}
    const bool automatic=provider=="AUTO" || provider=="Auto";
    if(!automatic && provider!="CPU" && provider!="CUDA" && provider!="DirectML") {
        m.info.reason="TARGET_LOAD_FAILED: Unknown execution provider";return;
    }
    std::vector<std::string> candidates;
    if(automatic) {
        const auto available=Ort::GetAvailableProviders();
        if(std::find(available.begin(),available.end(),"CUDAExecutionProvider")!=available.end()) candidates.emplace_back("CUDA");
        if(std::find(available.begin(),available.end(),"DmlExecutionProvider")!=available.end() ||
            std::find(available.begin(),available.end(),"DMLExecutionProvider")!=available.end()) candidates.emplace_back("DirectML");
    } else if(provider!="CPU") candidates.push_back(provider);
    candidates.emplace_back("CPU");
    std::string failures;
    for(const auto& candidate:candidates) try {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(2); options.SetInterOpNumThreads(1);
        options.SetExecutionMode(ORT_SEQUENTIAL); options.SetGraphOptimizationLevel(ORT_ENABLE_ALL);
        if(candidate=="CUDA") {
            OrtCUDAProviderOptions cuda{};cuda.device_id=0;options.AppendExecutionProvider_CUDA(cuda);
        } else if(candidate=="DirectML") {
#ifdef SVAI_ORT_DML
            options.DisableMemPattern(); const OrtDmlApi* dml=nullptr;
            Ort::ThrowOnError(Ort::GetApi().GetExecutionProviderApi("DML",ORT_API_VERSION,reinterpret_cast<const void**>(&dml)));
            Ort::ThrowOnError(dml->SessionOptionsAppendExecutionProvider_DML(options,0));
#else
            throw std::runtime_error("DirectML is not included in this build");
#endif
        }
        static std::atomic<std::uint64_t> counter{};
        const auto prefix=std::filesystem::temp_directory_path()/
            ("svai-target-warmup-"+std::to_string(static_cast<std::uint64_t>(clock_ms()*1000))+"-"+std::to_string(++counter));
        options.EnableProfiling(prefix.c_str());
        auto session=std::make_unique<Ort::Session>(m.env,path.c_str(),options);
        const auto meta=session->GetModelMetadata();
        if(metadata(meta,"svai.contract")!="target-roles-v1" ||
            metadata(meta,"svai.preprocess")!="full-rgb-bilinear-v1" || metadata(meta,"svai.roles")!="Wolf,Enemy")
            throw std::runtime_error("Incompatible target model metadata");
        if(session->GetInputCount()!=1 || session->GetOutputCount()!=2) throw std::runtime_error("Target tensor count mismatch");
        Ort::AllocatorWithDefaultOptions allocator;
        if(std::string(session->GetInputNameAllocated(0,allocator).get())!="frame") throw std::runtime_error("Expected target frame input");
        const auto input_type=session->GetInputTypeInfo(0); const auto tensor=input_type.GetTensorTypeAndShapeInfo();
        const auto shape=tensor.GetShape();
        if(tensor.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || shape!=std::vector<std::int64_t>{1,3,192,320})
            throw std::runtime_error("Expected target float input [1,3,192,320]");
        std::copy(shape.begin(),shape.end(),m.shape.begin());
        m.grid_height=24;m.grid_width=40;
        const std::array<std::vector<std::int64_t>,2> expected{
            std::vector<std::int64_t>{1,2,24,40},std::vector<std::int64_t>{1,2,4,24,40}};
        for(std::size_t i=0;i<2;++i) {
            if(std::string(session->GetOutputNameAllocated(i,allocator).get())!=output_names[i])
                throw std::runtime_error("Target output names/order mismatch");
            const auto output_type=session->GetOutputTypeInfo(i);const auto output=output_type.GetTensorTypeAndShapeInfo();
            if(output.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || output.GetShape()!=expected[i])
                throw std::runtime_error("Target output type/shape mismatch");
        }
        const auto threshold_text=metadata(meta,"svai.score_threshold");
        m.threshold=threshold_text.empty()?0.75:std::stod(threshold_text);
        if(!std::isfinite(m.threshold) || m.threshold<0.5 || m.threshold>1) throw std::runtime_error("Invalid target threshold");
        m.info.version=metadata(meta,"svai.model_version");
        if(m.info.version.empty() || m.info.version.size()>160) throw std::runtime_error("Missing target model version");
        m.info.trained=metadata(meta,"svai.training_status")=="trained";
        m.info.semantic_supported=m.info.trained && metadata(meta,"svai.semantic_supported")=="true";
        m.info.production_validated=m.info.semantic_supported && metadata(meta,"svai.production_validated")=="true";
        m.info.input_width=320;m.info.input_height=192;
        m.input.assign(static_cast<std::size_t>(3)*320*192,0.0f);
        auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        auto input=Ort::Value::CreateTensor<float>(memory,m.input.data(),m.input.size(),m.shape.data(),m.shape.size());
        for(int run=0;run<3;++run) validate_outputs(session->Run(Ort::RunOptions{nullptr},input_names,&input,1,output_names,2),24,40);
        m.info.provider=profiled_provider(*session);m.info.loaded=true;
        m.info.reason=m.info.production_validated?"TARGET_MODEL_READY":m.info.semantic_supported?
            "EXPERIMENTAL_TRAINED_ROLE_MODEL_UNVALIDATED":"TARGET_MODEL_UNTRAINED_NO_SEMANTIC_OUTPUT";
        if(!failures.empty())m.info.reason+="; provider fallback: "+failures;
        m.session=std::move(session);return;
    } catch(const std::exception& error) {
        if(!failures.empty()) failures+=" | ";
        failures+=candidate+": "+error.what();
    }
    m.info={};m.info.requested_provider=provider;m.info.reason="TARGET_LOAD_FAILED: "+failures;
}

std::vector<TargetDetection> TargetDetector::process(const SmallFrame& frame) {
    auto& m=*impl_;
    if(!m.session || !m.info.loaded) return {};
    if(!frame.color || !std::isfinite(frame.source_ms) || frame.source_ms<=0 || !frame.sequence || !frame.generation) {
        m.info.reason="TARGET_INVALID_FRAME";return {};
    }
    if(frame.generation==m.generation && (frame.sequence<=m.last_sequence || frame.source_ms<=m.last_source)) {
        m.info.reason="TARGET_OUT_OF_ORDER";return {};
    }
    try {
        const double preprocess_start=clock_ms();
        m.input=target_frame_rgb(*frame.color,m.info.input_width,m.info.input_height);
        m.info.preprocessing_ms=clock_ms()-preprocess_start;
        auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        auto input=Ort::Value::CreateTensor<float>(memory,m.input.data(),m.input.size(),m.shape.data(),m.shape.size());
        const double inference_start=clock_ms();
        const auto outputs=m.session->Run(Ort::RunOptions{nullptr},input_names,&input,1,output_names,2);
        m.info.inference_ms=clock_ms()-inference_start;
        validate_outputs(outputs,m.grid_height,m.grid_width);
        const float* scores=outputs[0].GetTensorData<float>();const float* boxes=outputs[1].GetTensorData<float>();
        const int area=m.grid_width*m.grid_height;
        std::vector<TargetDetection> result;
        for(int role=0;role<2;++role) {
            std::vector<TargetDetection> candidates;
            for(int cell=0;cell<area;++cell) {
                const double confidence=scores[role*area+cell];
                if(confidence<m.threshold) continue;
                const auto coordinate=[&](int c){return static_cast<double>(boxes[(role*4+c)*area+cell]);};
                const TargetBox box{coordinate(0),coordinate(1),coordinate(2),coordinate(3)};
                if(!box.valid())continue;
                candidates.push_back({role==0?TargetRole::Wolf:TargetRole::Enemy,box,confidence,
                    frame.source_ms,frame.sequence,frame.generation,0,m.info.semantic_supported});
            }
            std::stable_sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.confidence>b.confidence;});
            if(candidates.size()>64)candidates.resize(64);
            std::vector<TargetDetection> selected;
            for(const auto& candidate:candidates) {
                if(std::none_of(selected.begin(),selected.end(),[&](const auto& old){return iou(candidate.box,old.box)>0.4;}))
                    selected.push_back(candidate);
                if(selected.size()==8)break;
            }
            result.insert(result.end(),selected.begin(),selected.end());
        }
        m.generation=frame.generation;m.last_sequence=frame.sequence;m.last_source=frame.source_ms;
        m.info.reason=m.info.production_validated?"TARGET_MODEL_READY":m.info.semantic_supported?
            "EXPERIMENTAL_TRAINED_ROLE_MODEL_UNVALIDATED":"TARGET_MODEL_UNTRAINED_NO_SEMANTIC_OUTPUT";
        return result;
    } catch(const std::exception& error) {
        m.session.reset();m.info.loaded=m.info.semantic_supported=false;
        m.info.reason="TARGET_INFERENCE_FAILED: "+std::string(error.what());return {};
    }
}
}
