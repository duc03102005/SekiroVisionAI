#include <SekiroVisionAI/TargetDetector.h>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

using namespace sekiro;
namespace {
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
ColorFrame color_fixture() {
    ColorFrame frame;frame.width=7;frame.height=5;frame.stride=32;frame.bgra.assign(160,0);
    for(int y=0;y<5;++y)for(int x=0;x<7;++x) {
        const auto at=static_cast<std::size_t>(y)*frame.stride+x*4;
        frame.bgra[at]=30;frame.bgra[at+1]=60;frame.bgra[at+2]=120;frame.bgra[at+3]=255;
    }
    return frame;
}
std::shared_ptr<ColorFrame> read_frame(const char* path) {
    std::ifstream input(path,std::ios::binary);
    std::array<std::int32_t,3> shape{};input.read(reinterpret_cast<char*>(shape.data()),sizeof(shape));
    if(!input || shape[0]<1 || shape[0]>8192 || shape[1]<1 || shape[1]>8192 || shape[2]!=shape[0]*4)
        throw std::runtime_error("Invalid actual-frame fixture header");
    auto color=std::make_shared<ColorFrame>();color->width=shape[0];color->height=shape[1];color->stride=shape[2];
    color->bgra.resize(static_cast<std::size_t>(color->stride)*color->height);
    input.read(reinterpret_cast<char*>(color->bgra.data()),static_cast<std::streamsize>(color->bgra.size()));
    if(!input)throw std::runtime_error("Truncated actual-frame fixture");
    return color;
}
}
int main(int argc,char** argv) {try {
    auto color=color_fixture();const auto tensor=target_frame_rgb(color,320,192);
    const std::size_t plane=320*192;
    require(tensor.size()==plane*3,"Target preprocessing shape mismatch");
    for(std::size_t i=0;i<plane;++i) {
        require(std::abs(tensor[i]-120.0f/255)<1e-6f,"Target preprocessing must use RGB and honor padded rows");
        require(std::abs(tensor[plane+i]-60.0f/255)<1e-6f,"Target green channel mismatch");
        require(std::abs(tensor[2*plane+i]-30.0f/255)<1e-6f,"Target blue channel mismatch");
    }
    color.stride=4;bool rejected=false;
    try {target_frame_rgb(color,320,192);}catch(const std::exception&){rejected=true;}
    require(rejected,"Malformed frame stride must fail before interpolation");
    TargetDetector detector;detector.load("this-target-model-does-not-exist.onnx");
    require(!detector.status().loaded && detector.process({}).empty(),"Missing target weights cannot fabricate detections");
    if(argc>1) {
        detector.load(argv[1],"CPU");
        const auto status=detector.status();
        require(status.loaded && status.input_width==320 && status.input_height==192,"Native ONNX target model must load and warm up");
        require(status.provider=="CPU","Provider display must reflect observed warmup nodes");
        SmallFrame frame;frame.source_ms=1000;frame.sequence=1;frame.generation=1;
        frame.color=argc>2?read_frame(argv[2]):std::make_shared<ColorFrame>(color_fixture());
        const auto detections=detector.process(frame);
        for(const auto& detected:detections) {
            require(detected.box.valid() && detected.confidence>=0.5 && detected.confidence<=1,"Native model boxes/confidence must satisfy contract");
            require(detected.source_ms==1000 && detected.sequence==1 && detected.generation==1,"Detection must retain original frame provenance");
            require(detected.semantic_supported==status.semantic_supported,"Untrained models cannot assert supported semantic identity");
        }
        if(argc>3) {
            std::ofstream output(argv[3]);output<<std::setprecision(10)<<"[";
            for(std::size_t i=0;i<detections.size();++i) {
                const auto& d=detections[i];if(i)output<<',';
                output<<"{\"role\":\""<<(d.role==TargetRole::Wolf?"Wolf":"Enemy")<<"\",\"confidence\":"<<d.confidence
                    <<",\"box\":["<<d.box.left<<','<<d.box.top<<','<<d.box.right<<','<<d.box.bottom<<"]}";
            }
            output<<"]\n";
            const auto rgb=target_frame_rgb(*frame.color,320,192);
            std::ofstream floats(std::string(argv[3])+".rgb-f32",std::ios::binary);
            floats.write(reinterpret_cast<const char*>(rgb.data()),static_cast<std::streamsize>(rgb.size()*sizeof(float)));
        }
        require(detector.process(frame).empty() && detector.status().reason=="TARGET_OUT_OF_ORDER",
            "Duplicate frames must never fabricate fresh semantic observations");
        frame.sequence=2;frame.source_ms=1020;detector.process(frame);
        require(detector.status().loaded,"A duplicate frame rejection should not destroy the model");
        frame.generation=2;frame.sequence=1;frame.source_ms=1040;detector.process(frame);
        require(detector.status().loaded,"New capture generation may start a new detector sequence");
    }
    std::cout<<"Native target runtime tests passed (software checks; no all-boss accuracy claim).\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
