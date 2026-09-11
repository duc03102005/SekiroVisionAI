#include <SekiroVisionAI/TemporalModel.h>
#include <cmath>
#include <iostream>
#include <stdexcept>
using namespace sekiro;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(int argc,char** argv){try {
    require(argc==2||argc==3,"Expected fixture model path and optional temporal-v2 fixture path");
    TemporalModel model;model.load(std::filesystem::path(argv[1]),"CPU");const auto status=model.status();
    require(status.loaded,status.reason.c_str());require(!status.trained&&!status.auto_eligible,"Test model cannot be gameplay model");
    require(status.warmup_runs==3&&status.warmup_ms>=0,"Session must execute warmup before readiness");
    require(status.provider=="CPU","CPU provider must be observed in warmup node profile");
    auto color=std::make_shared<ColorFrame>();color->width=640;color->height=360;color->stride=640*4;color->bgra.resize(640*360*4);
    for(std::size_t i=0;i<color->bgra.size();i+=4){color->bgra[i]=10;color->bgra[i+1]=70;color->bgra[i+2]=200;color->bgra[i+3]=255;}
    const auto crop=model_crop_rgb(*color,CombatRoi{},320);
    require(std::abs(crop[0]-200.0/255)<1e-6&&std::abs(crop[320*320]-70.0/255)<1e-6&&std::abs(crop[2*320*320]-10.0/255)<1e-6,"BGRA to RGB normalization must match training");
    SmallFrame frame;frame.color=color;frame.generation=1;ModelPrediction prediction;int accepted=0;
    // 144Hz source must make approximately 30Hz observations, not 36Hz.
    for(int i=0;i<144;++i){frame.sequence=i+1;frame.source_ms=1000+i*(1000.0/144);prediction=model.process(frame,{});
        if(prediction.reason!="MODEL_SAMPLE_INTERVAL")++accepted;
        if(prediction.valid) {
            require(!prediction.trained&&!prediction.auto_eligible,"Synthetic result cannot enable input");
            require(!prediction.state_supported&&prediction.state==9,"Unsupported state logits must not look recognized");
            require(!prediction.class_supported&&prediction.attack_class==13,"Unsupported class logits must not affect Dodge policy");
        }}
    require(accepted>=29&&accepted<=31,"Persistent sampling grid must retain 30Hz cadence");
    require(model.status().history_size==16,"Temporal history should warm");
    frame.sequence=200;frame.source_ms=4000;prediction=model.process(frame,{});
    require(!prediction.valid&&prediction.reason=="TEMPORAL_WARMUP","Long source gap resets causal history");
    model.load(std::filesystem::path(argv[1]),"AUTO");
    require(model.status().loaded,"AUTO should select a working available provider");
    require(model.status().requested_provider=="AUTO"&&model.status().warmup_runs==3,"AUTO selection must retain actual startup evidence");
    require(!model.status().provider_selection.empty(),"Selection status must explain executing providers");
    model.load(std::filesystem::path(argv[1]),"DirectML");
    require(model.status().loaded,"Missing/unusable DirectML should fall back to a working CPU session");
    if(model.status().provider_selection.find("previous attempts")!=std::string::npos)
        require(model.status().provider_fallback&&model.status().provider=="CPU","Failed accelerator attempt must report actual CPU fallback");
    if(argc==3) {
        model.load(std::filesystem::path(argv[2]),"CPU");
        require(model.status().loaded&&model.status().contract=="temporal-v2","v2 fixture must load alongside v1");
        for(int i=0;i<18;++i){frame.sequence=1000+i;frame.source_ms=6000+i*(1000.0/30);prediction=model.process(frame,{});}
        require(prediction.valid,"v2 graph must infer after causal warmup");
        require(!prediction.attack_direction_supported&&prediction.attack_direction==7&&prediction.attack_direction_confidence==0,
                "Unsupported trajectory logits must not become a confident direction");
        require(!prediction.trained&&!prediction.auto_eligible,"v2 synthetic model never enables Auto Dodge");
    }
    model.load(std::filesystem::path(argv[1]),"NonexistentProvider");
    require(!model.status().loaded,"Invalid explicit provider must not silently pretend success");
    model.load(std::filesystem::path(argv[1]).parent_path()/"missing.onnx","CPU");require(!model.status().loaded,"Missing model must unload old model");
    std::cout<<"Native ORT load/preprocess/cadence/gap tests passed; fixture is not gameplay AI.\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
