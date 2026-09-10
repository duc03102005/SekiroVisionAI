#include <SekiroVisionAI/ThreatTracker.h>
#include <cstdlib>
#include <iostream>

namespace {
void require(bool condition,const char* message){if(!condition){std::cerr<<"FAIL: "<<message<<'\n';std::exit(1);}}
sekiro::MotionSignals signal(double time,double score,bool camera=false) {
    sekiro::MotionSignals s;s.source_ms=time;s.score=score;s.confidence=0.9;s.changed_fraction=score>0.4?0.2:0;
    s.acceleration=0.5;s.valid=true;s.camera_only=camera;s.reason=camera?"CAMERA_ONLY":"LOCAL_MOTION";return s;
}
int pattern(int x,int y){return 35+((x*17+y*29+(x*y)%67)%80);}
sekiro::SmallFrame image(int frame,int camera_x=0,int weapon_shift=0) {
    sekiro::SmallFrame f;f.sequence=static_cast<std::uint64_t>(frame+1);f.generation=1;f.source_ms=1000+frame*16.667;f.ready_ms=f.source_ms+2;
    for(int y=0;y<sekiro::vision_height;++y)for(int x=0;x<sekiro::vision_width;++x){
        const int scene_x=x-camera_x;
        int v=pattern(scene_x+1000,y+1000);
        // Textured training dummy and a moving test rectangle; deliberately synthetic.
        if(scene_x>=99+weapon_shift&&scene_x<147+weapon_shift&&y>=28&&y<78)v=140+(((scene_x-weapon_shift)*23+y*11)%110);
        f.gray[static_cast<std::size_t>(y*sekiro::vision_width+x)]=static_cast<std::uint8_t>(v);
    }
    return f;
}
}
int main(){
    using namespace sekiro;
    ThreatTracker tracker;ThreatConfig config;
    for(int i=0;i<60;++i)require(!tracker.step(signal(1000+i*16.667,0),1003+i*16.667,config).trigger,"idle never dodges");
    int actions=0;
    for(int i=60;i<180;++i)actions+=tracker.step(signal(1000+i*16.667,0.9),1003+i*16.667,config).trigger;
    require(actions==1,"sustained threat produces exactly one action even beyond cooldown");
    for(int i=180;i<210;++i)require(!tracker.step(signal(1000+i*16.667,0.9,true),1003+i*16.667,config).trigger,"camera cannot retire consumed token");
    for(int i=210;i<225;++i)require(!tracker.step(signal(1000+i*16.667,0.9),1003+i*16.667,config).trigger,"camera gap cannot create duplicate action");
    for(int i=225;i<245;++i)tracker.step(signal(1000+i*16.667,0),1003+i*16.667,config);
    for(int i=245;i<265;++i)actions+=tracker.step(signal(1000+i*16.667,0.9),1003+i*16.667,config).trigger;
    require(actions==2,"quiet boundary permits a new episode");
    tracker.reset();
    for(int i=0;i<60;++i)tracker.step(signal(1000+i*16.667,0),1003+i*16.667,config);
    require(!tracker.step(signal(2000,0.9),2003,config).trigger,"one high frame is only a candidate");
    require(!tracker.step(signal(2017,0.1),2020,config).trigger,"feint cancels before arming");
    require(!tracker.step(signal(2100,0.99),2400,config).trigger,"stale prediction cannot act");
    require(!tracker.step(signal(2000,0.99),2003,config).trigger,"out-of-order cannot act");

    DispatchGuard guard;
    DispatchContext context{true,true,true,true,false,1000,1005,650,3,3,1};
    require(guard.reserve(context)==nullptr,"valid first dispatch reserved");
    context.source_ms=2000;context.now_ms=2005;
    require(std::string(guard.reserve(context))=="CONSUMED_EPISODE","dispatch dedupe survives cooldown expiry");
    context.episode=2;context.enabled=false;
    require(std::string(guard.reserve(context))=="DISABLED","disabled dispatch rejected");
    context.enabled=true;context.foreground=false;
    require(std::string(guard.reserve(context))=="LOST_FOCUS","focus checked before dispatch");
    context.foreground=true;context.revision=2;
    require(std::string(guard.reserve(context))=="OLD_ARM_REVISION","queued work rejected after rearm");
    context.revision=3;context.key_conflict=true;
    require(std::string(guard.reserve(context))=="PHYSICAL_KEY_CONFLICT","physical key ownership respected");

    MotionDetector detector;CombatRoi roi;
    for(int i=0;i<60;++i){auto s=detector.process(image(i),roi);require(s.score<0.01,"actual pixel idle suppression");}
    auto camera=detector.process(image(60,3),roi);
    require(camera.camera_dx==3&&camera.score<0.05,"global camera translation compensated before scoring");
    detector.reset();tracker.reset();
    for(int i=0;i<100;++i){auto f=image(i,i*2);auto s=detector.process(f,roi);require(!tracker.step(s,f.source_ms+3,config).trigger,"continuous camera-only pixel sequence never dodges");}
    detector.reset();tracker.reset();actions=0;double peak=0;
    for(int i=0;i<60;++i){auto f=image(i);auto s=detector.process(f,roi);actions+=tracker.step(s,f.source_ms+3,config).trigger;}
    for(int i=60;i<78;++i){auto f=image(i,0,(i-59)*2);auto s=detector.process(f,roi);peak=std::max(peak,s.score);actions+=tracker.step(s,f.source_ms+3,config).trigger;}
    std::cout<<"Synthetic local-motion peak score="<<peak<<" actions="<<actions<<'\n';
    require(actions==1,"real pixel sequence flows through CV and temporal detector to one Dodge request");
    SmallFrame black;black.generation=1;black.source_ms=3000;detector.reset();detector.process(black,roi);black.source_ms+=17;
    require(!detector.process(black,roi).valid,"black/textureless ROI cannot create a threat");
    for(double step:{1000.0/30,1000.0/60,1000.0/120}){
        tracker.reset();actions=0;
        for(double t=1000;t<2000;t+=step)tracker.step(signal(t,0),t+2,config);
        for(double t=2100;t<4000;t+=step)actions+=tracker.step(signal(t,0.9),t+2,config).trigger;
        require(actions==1,"one-episode policy is stable at 30/60/120 source FPS");
    }
    std::cout<<"Auto Dodge software behavior: PASS (synthetic; no in-game accuracy claim)\n";
}
