#include <SekiroVisionAI/TargetTracker.h>
#include <iostream>
#include <limits>
#include <stdexcept>

using namespace sekiro;
namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }

SmallFrame scene(std::uint64_t sequence, int camera_x=0, int camera_y=0, int object_x=-1,
    std::uint64_t generation=1) {
    SmallFrame frame; frame.sequence=sequence; frame.generation=generation;
    frame.source_ms=1000+static_cast<double>(sequence)*20;
    frame.ready_ms=frame.source_ms+5; frame.source_width=1920; frame.source_height=1080;
    for(int y=0;y<vision_height;++y) for(int x=0;x<vision_width;++x) {
        const int sx=x-camera_x, sy=y-camera_y;
        const auto hash=static_cast<unsigned>((sx+1024)*73856093u)^static_cast<unsigned>((sy+1024)*19349663u);
        int value=40+static_cast<int>(hash%145);
        if(object_x>=0 && sx>=object_x && sx<object_x+32 && sy>=34 && sy<66)
            value=((sx-object_x)/3+sy/3)%2 ? 235 : 15;
        frame.gray[static_cast<std::size_t>(y*vision_width+x)]=static_cast<std::uint8_t>(value);
    }
    return frame;
}

std::array<TargetDetection,2> actors(const SmallFrame& frame,double enemy_shift=0,std::uint64_t enemy_id=40) {
    return {{
        {TargetRole::Wolf,{0.43,0.59,0.57,0.86},0.96,frame.source_ms,frame.sequence,frame.generation,8,true},
        {TargetRole::Enemy,{0.36+enemy_shift,0.18,0.55+enemy_shift,0.52},0.97,
            frame.source_ms,frame.sequence,frame.generation,enemy_id,true}
    }};
}

void camera_only() {
    TargetTracker tracker;
    for(std::uint64_t i=1;i<=8;++i) {
        const auto state=tracker.process(scene(i,static_cast<int>(i)*2,static_cast<int>(i)));
        require(!state.valid,"Camera-only translation must not acquire a target");
        require(!state.identity_certain,"Background texture must not fabricate semantic identity");
        if(i>1) require(state.camera_reliable && state.camera_dx==2 && state.camera_dy==1,
            "Tracker must recover known background translation");
    }
}

void follow_visual_region() {
    TargetTracker tracker;
    TargetState state;
    double initial_center=0;
    std::uint64_t lineage=0;
    for(std::uint64_t i=1;i<=15;++i) {
        state=tracker.process(scene(i,static_cast<int>(i),0,88+static_cast<int>(i)*2));
        if(i==7) {
            require(state.valid,"Local moving textured region must acquire automatically after confirmation");
            require(state.roi.valid(),"Automatic crop must stay within native preprocessing ROI contract");
            initial_center=state.enemy.box.center_x(); lineage=state.track_lineage;
        }
        if(i>7) require(state.valid && state.track_lineage==lineage,"Continuous target must retain lineage");
        require(!state.identity_certain && !state.wolf.valid && !state.enemy.semantic_confirmed,
            "Motion tracking must never be labeled Wolf/enemy semantic detection");
        require(!state.attack_path.valid && !state.escape.left.valid,
            "Observed region movement does not establish weapon trajectory or safe escape");
    }
    require(state.enemy.box.center_x()>initial_center+0.06,"Automatic ROI must follow target and camera movement");
    require(state.roi.left<=state.enemy.box.left && state.roi.right>=state.enemy.box.right,
        "Dynamic crop must contain tracked target");

    // An occluding/unrelated image invalidates action geometry immediately; old
    // boxes may be held for display but cannot stay currently observed.
    auto occlusion=scene(16); occlusion.gray.fill(0);
    require(!tracker.process(occlusion).valid,"Black transition must invalidate target immediately");
    require(!tracker.process(scene(17)).identity_certain,"Reacquisition cannot inherit semantic certainty");
}

void semantic_identity_and_freshness() {
    TargetTracker tracker;
    TargetState state;
    for(std::uint64_t i=1;i<=6;++i) {
        const auto frame=scene(i); const auto detected=actors(frame,0.002*static_cast<double>(i));
        state=tracker.process(frame,detected);
        if(i<5) require(!state.identity_certain,"Semantic identities require temporal confirmation");
    }
    require(state.identity_certain && state.wolf.semantic_confirmed && state.enemy.semantic_confirmed,
        "Fresh consistent trained role detections must confirm both semantic tracks");
    const auto lineage=state.track_lineage;
    require(state.roi.left<=state.enemy.box.left && state.roi.right>=state.wolf.box.right &&
        state.roi.bottom>=state.wolf.box.bottom,"Combat ROI must include both observed actors");

    auto frame=scene(7); auto stale=actors(frame);
    for(auto& d:stale) { d.source_ms-=20; --d.sequence; }
    state=tracker.process(frame,stale);
    require(!state.valid && !state.identity_certain && state.held,
        "Stale detections must hold display only, never relabel old boxes current");
    require(state.track_lineage==lineage && state.enemy.age_ms==20,
        "A missing observation retains lineage and true source age");

    frame=scene(8); state=tracker.process(frame,actors(frame));
    require(state.identity_certain && state.track_lineage==lineage,
        "Same semantic target can recover fresh evidence without creating a new strike identity");

    // A detector-provided new ID cannot silently take over an old target slot.
    frame=scene(9); state=tracker.process(frame,actors(frame,0.35,91));
    require(!state.identity_certain && state.track_lineage==lineage,
        "Different enemy ID must abstain while old target lineage is held");
    for(std::uint64_t i=10;i<=24;++i) {
        frame=scene(i); state=tracker.process(frame,actors(frame,0.35,91));
    }
    require(state.identity_certain && state.track_lineage!=lineage,
        "New enemy may acquire only after old observation retirement and new confirmation");

    const auto before_generation=state.track_lineage;
    frame=scene(25,0,0,-1,2); state=tracker.process(frame,actors(frame));
    require(!state.identity_certain && state.track_lineage!=before_generation,
        "Capture generation change must reset confirmation and allocate a new lineage");
}

void invalid_unsupported_and_ambiguous() {
    TargetTracker tracker;
    for(std::uint64_t i=1;i<=8;++i) {
        auto frame=scene(i); auto generic=actors(frame);
        for(auto& detection:generic) detection.semantic_supported=false;
        require(!tracker.process(frame,generic).identity_certain,"Generic detector labels cannot establish Wolf or enemy roles");
    }
    tracker.reset();
    auto frame=scene(1); auto detected=actors(frame);
    detected[1].confidence=std::numeric_limits<double>::quiet_NaN();
    require(!tracker.process(frame,detected).enemy.valid,"Nonfinite confidence must not enter a target track");
    frame=scene(2); detected=actors(frame); detected[1].box.left=-0.1;
    require(!tracker.process(frame,detected).enemy.valid,"Out-of-frame boxes must not enter a target track");

    tracker.reset();
    for(std::uint64_t i=1;i<=6;++i) {
        frame=scene(i); detected=actors(frame);
        std::array<TargetDetection,3> crowded{detected[0],detected[1],detected[1]};
        crowded[2].box={0.45,0.18,0.64,0.52}; crowded[2].detector_track_id=41;
        const auto state=tracker.process(frame,crowded);
        require(!state.identity_certain && std::string(state.reason)=="TARGET_AMBIGUOUS",
            "Two similarly plausible enemy identities must abstain");
    }

    tracker.reset();
    for(std::uint64_t i=1;i<=6;++i) {frame=scene(i); tracker.process(frame,actors(frame));}
    const auto duplicate=tracker.process(frame,actors(frame));
    require(!duplicate.valid && std::string(duplicate.reason)=="TARGET_OUT_OF_ORDER",
        "Duplicate frames must not advance tracker time or return valid action geometry");
    frame=scene(20);
    require(!tracker.process(frame,actors(frame)).identity_certain,"Long frame gap must restart semantic confirmation");
    frame.source_ms=std::numeric_limits<double>::infinity();
    require(!tracker.process(frame).valid,"Nonfinite timestamp must fail closed");
}

void confirmation_gaps_and_wolf_lineage() {
    TargetTracker tracker;
    TargetState state;
    for(std::uint64_t i=1;i<=3;++i) { const auto frame=scene(i); tracker.process(frame,actors(frame)); }
    tracker.process(scene(4));
    for(std::uint64_t i=5;i<=7;++i) {
        const auto frame=scene(i); state=tracker.process(frame,actors(frame));
        require(!state.identity_certain,"An unconfirmed identity cannot count a missing observation toward dwell");
    }
    for(std::uint64_t i=8;i<=9;++i) { const auto frame=scene(i); state=tracker.process(frame,actors(frame)); }
    require(state.identity_certain,"Renewed uninterrupted semantic observations can confirm again");
    const auto lineage=state.track_lineage, enemy_id=state.enemy.track_id;
    for(std::uint64_t i=10;i<=25;++i) {
        const auto frame=scene(i); auto detections=actors(frame); detections[0].detector_track_id=9;
        state=tracker.process(frame,detections);
        if(i<19) require(!state.identity_certain,"Changed Wolf identity must invalidate current threat geometry");
    }
    require(state.identity_certain && state.enemy.track_id==enemy_id && state.track_lineage!=lineage,
        "Actor-pair lineage must change when Wolf changes even if enemy identity stays stable");

    // Omitting a provider ID for one observation must not erase a known ID and
    // permit another identity to take over on the following frame.
    auto frame=scene(26); auto detections=actors(frame); detections[0].detector_track_id=9;
    detections[1].detector_track_id=0; tracker.process(frame,detections);
    frame=scene(27); detections=actors(frame); detections[0].detector_track_id=9; detections[1].detector_track_id=999;
    require(!tracker.process(frame,detections).identity_certain,"Missing provider ID must not erase established identity association");

    tracker.reset();
    auto color=std::make_shared<ColorFrame>(); std::weak_ptr<const ColorFrame> retained=color;
    frame=scene(1); frame.color=color; tracker.process(frame);
    frame.color.reset(); color.reset();
    require(retained.expired(),"Tracker grayscale history must not retain capture color ownership");
}
}

int main() { try {
    camera_only(); follow_visual_region(); semantic_identity_and_freshness(); invalid_unsupported_and_ambiguous();
    confirmation_gaps_and_wolf_lineage();
    std::cout<<"Target tracker cases passed: automatic visual ROI, camera-only rejection, semantic contract, freshness, lineage.\n";
    return 0;
} catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; } }
