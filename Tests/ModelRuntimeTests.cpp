#include <SekiroVisionAI/TemporalModel.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace sekiro;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(int argc,char** argv){try {
    require(argc==2,"Expected synthetic fixture model path");
    TemporalModel model;model.load(std::filesystem::path(argv[1]),"CPU");const auto status=model.status();
    require(status.loaded,status.reason.c_str());require(!status.trained&&!status.auto_eligible,"Test model cannot be gameplay model");
    auto color=std::make_shared<ColorFrame>();color->width=640;color->height=360;color->stride=640*4;color->bgra.resize(640*360*4);
    for(std::size_t i=0;i<color->bgra.size();i+=4){color->bgra[i]=10;color->bgra[i+1]=70;color->bgra[i+2]=200;color->bgra[i+3]=255;}
    const auto crop=model_crop_rgb(*color,CombatRoi{},320);
    require(std::abs(crop[0]-200.0/255)<1e-6&&std::abs(crop[320*320]-70.0/255)<1e-6&&std::abs(crop[2*320*320]-10.0/255)<1e-6,"BGRA to RGB normalization must match training");
    SmallFrame frame;frame.color=color;frame.generation=1;ModelPrediction prediction;int accepted=0;
    // 144Hz source must make approximately 30Hz observations, not 36Hz.
    for(int i=0;i<144;++i){frame.sequence=i+1;frame.source_ms=1000+i*(1000.0/144);prediction=model.process(frame,{});
        if(prediction.reason!="MODEL_SAMPLE_INTERVAL")++accepted;
        if(prediction.valid)require(!prediction.trained&&!prediction.auto_eligible,"Synthetic result cannot enable input");}
    require(accepted>=29&&accepted<=31,"Persistent sampling grid must retain 30Hz cadence");
    require(model.status().history_size==16,"Temporal history should warm");
    frame.sequence=200;frame.source_ms=4000;prediction=model.process(frame,{});
    require(!prediction.valid&&prediction.reason=="TEMPORAL_WARMUP","Long source gap resets causal history");
    model.load(std::filesystem::path(argv[1]).parent_path()/"missing.onnx","CPU");require(!model.status().loaded,"Missing model must unload old model");
    std::cout<<"Native ORT load/preprocess/cadence/gap tests passed; fixture is not gameplay AI.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
