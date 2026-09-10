#include <SekiroVisionAI/TemporalDecision.h>
#include <iostream>
#include <stdexcept>

using namespace sekiro;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
ModelPrediction sample(double source,double attack=0.95,double threat=0.95,double tti=110) {
    ModelPrediction p;p.valid=p.trained=p.attack_supported=p.threat_supported=p.tti_supported=p.auto_eligible=true;
    p.source_ms=source;p.attack_probability=attack;p.threat_probability=threat;p.tti_ms=tti;p.tti_uncertainty_ms=5;p.attack_class=0;return p;
}
void quiet(TemporalDecision& engine,double begin,const TemporalPolicy& policy){for(int i=0;i<8;++i)engine.step(sample(begin+i*33,0.01,0.01),begin+i*33+10,policy);}
int main(){try {
    TemporalPolicy policy;TemporalDecision engine;
    require(!engine.step(sample(1000),1010,policy).decision.trigger,"Cannot begin on an already active strike");
    quiet(engine,1100,policy);
    require(!engine.step(sample(1400),1410,policy).decision.trigger,"First high frame needs dwell");
    require(engine.step(sample(1433),1443,policy).decision.trigger,"High threat + valid TTI must reach real dispatch path");
    for(int i=1;i<50;++i)require(!engine.step(sample(1433+i*33),1443+i*33,policy).decision.trigger,"Never repeat one threat after cooldown");
    quiet(engine,3200,policy);
    require(!engine.step(sample(3500),3510,policy).decision.trigger,"New strike starts dwell");
    require(engine.step(sample(3533),3543,policy).decision.trigger,"Quiet and cooldown admit next strike");

    engine.reset();quiet(engine,1000,policy);engine.step(sample(1300),1310,policy);
    for(int i=1;i<10;++i)engine.step(sample(1300+i*33,0.01,0.95),1310+i*33,policy);
    require(!engine.step(sample(1633),1643,policy).decision.trigger,"Lost attack head must cancel prior dwell");
    require(engine.step(sample(1666),1676,policy).decision.trigger,"Renewed sustained evidence can arm");

    for(int mode=0;mode<7;++mode){
        engine.reset();quiet(engine,1000,policy);engine.step(sample(1300),1310,policy);auto p=sample(1333);
        if(mode==0)p.trained=false;
        if(mode==1)p.tti_supported=false;
        if(mode==2)p.attack_supported=false;
        if(mode==3)p.auto_eligible=false;
        if(mode==4)p.tti_ms=20;
        if(mode==5)p.tti_uncertainty_ms=200;
        if(mode==6)p.attack_class=4;
        require(!engine.step(p,1343,policy).decision.trigger,"Unsupported/untrained/late/uncertain/sweep must abstain");
    }
    engine.reset();quiet(engine,1000,policy);engine.step(sample(1300),1310,policy);
    require(!engine.step(sample(1300),1343,policy).decision.trigger,"Duplicate timestamps never advance dwell");
    require(!engine.step(sample(1800),1810,policy).decision.trigger,"Gap requires new quiet; no immediate action");
    std::cout<<"Temporal decision cases passed (synthetic traces, no game/input).\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
