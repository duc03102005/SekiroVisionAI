#pragma once
#include <SekiroVisionAI/MotionDetector.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace sekiro {

// All coordinates refer to the uncropped game image, with +x right and +y down.
// Screen-space boxes/motion do not establish world geometry or safe movement.
struct TargetBox {
    double left{}, top{}, right{}, bottom{};
    bool valid() const noexcept {
        return std::isfinite(left) && std::isfinite(top) && std::isfinite(right) && std::isfinite(bottom) &&
            left >= 0 && top >= 0 && right <= 1 && bottom <= 1 && right-left >= 0.02 && bottom-top >= 0.02;
    }
    double center_x() const noexcept { return (left + right) * 0.5; }
    double center_y() const noexcept { return (top + bottom) * 0.5; }
};

enum class TargetRole { Wolf, Enemy };

// A learned detector can supply these observations without replacing tracking,
// target selection, ROI creation, or the replay pipeline. Every observation must
// identify its original frame. Re-stamping an old detection is forbidden.
struct TargetDetection {
    TargetRole role{TargetRole::Enemy};
    TargetBox box{};
    double confidence{};
    double source_ms{};
    std::uint64_t sequence{}, generation{}, detector_track_id{};
    // True only for a detector whose trained labels support this semantic role.
    // A generic person label, motion blob, or untrained model does not qualify.
    bool semantic_supported{};
};

struct TargetObservation {
    TargetBox box{};
    bool valid{}, semantic_confirmed{}, observed_this_frame{};
    double confidence{}, age_ms{};
    std::uint64_t track_id{};
};

struct AttackPath {
    bool valid{};
    double dx{}, dy{}, confidence{};
    const char* coordinate_frame{"SCREEN"};
};

struct EscapeCandidate {
    bool valid{};
    double clearance{}, confidence{};
};

struct EscapeEvidence {
    EscapeCandidate left{}, right{}, forward{}, backward{};
};

struct TargetState {
    CombatRoi roi{};
    TargetObservation wolf{}, enemy{};
    std::uint64_t track_lineage{}, sequence{}, generation{};
    double source_ms{}, camera_dx{}, camera_dy{}, camera_error{};
    bool valid{}, identity_certain{}, camera_reliable{}, held{};
    // Remain invalid unless a dedicated geometry model supplies this evidence.
    // Target displacement is deliberately never relabeled weapon/attack motion.
    AttackPath attack_path{};
    EscapeEvidence escape{};
    const char* reason{"TARGET_WARMUP"};
};

struct TargetTrackerConfig {
    double semantic_confidence{0.75};
    double semantic_confirmation_ms{65};
    double visual_confirmation_ms{45};
    double hold_ms{180};
    double max_gap_ms{120};
    bool allow_provisional{true};
};

// Bounded, causal image-space tracker. The fallback finds and follows visual
// motion regions with camera compensation; it never claims to recognize Wolf,
// a boss, a weapon, an attack, or a safe escape. Strict production decisions
// require identity_certain and a trained semantic detector supplying both roles.
class TargetTracker {
public:
    explicit TargetTracker(TargetTrackerConfig config = {}) : config_(validated(config)) {}
    void reset() noexcept {
        previous_ = {}; have_previous_ = false; enemy_ = {}; wolf_ = {};
        lineage_ = lineage_enemy_ = lineage_wolf_ = 0;
        // Never recycle an ID after a discontinuity within this tracker lifetime.
    }
    TargetState process(const SmallFrame& frame, std::span<const TargetDetection> detections = {});

private:
    struct Track {
        TargetBox box{};
        std::uint64_t id{}, external_id{};
        double first_ms{}, last_seen_ms{}, confidence{};
        bool exists{}, semantic{}, confirmed{}, observed{};
    };
    struct CameraMotion {
        int dx{}, dy{};
        double error{}, brightness{};
        bool reliable{}, cut{};
    };
    struct Region {
        TargetBox box{};
        double mass{}, confidence{};
    };
    static double overlap(const TargetBox& a, const TargetBox& b) noexcept;
    static TargetTrackerConfig validated(TargetTrackerConfig config) noexcept {
        const auto value=[](double requested,double fallback,double low,double high) {
            return std::isfinite(requested)?std::clamp(requested,low,high):fallback;
        };
        config.semantic_confidence=value(config.semantic_confidence,0.75,0.5,1.0);
        config.semantic_confirmation_ms=value(config.semantic_confirmation_ms,65,20,500);
        config.visual_confirmation_ms=value(config.visual_confirmation_ms,45,20,500);
        config.hold_ms=value(config.hold_ms,180,0,500);
        config.max_gap_ms=value(config.max_gap_ms,120,20,250);
        return config;
    }
    static TargetBox translated(TargetBox box, double dx, double dy) noexcept;
    static CombatRoi combat_roi(const TargetBox& enemy, const TargetBox* wolf) noexcept;
    static int pixel(const SmallFrame& frame, int x, int y) noexcept {
        return frame.gray[static_cast<std::size_t>(y * vision_width + x)];
    }
    CameraMotion camera_motion(const SmallFrame& frame) const;
    std::vector<Region> motion_regions(const SmallFrame& frame, const CameraMotion& camera) const;
    bool follow_patch(const SmallFrame& frame, const CameraMotion& camera, TargetBox& tracked, double& quality) const;
    void observe(Track& track, const TargetBox& box, double confidence, double time,
        bool semantic, std::uint64_t external_id = 0);
    bool update_semantic(Track& track, TargetRole role, const SmallFrame& frame,
        std::span<const TargetDetection> detections, bool& ambiguous);
    static TargetObservation observation(const Track& track, double time) noexcept;
    void remember(const SmallFrame& frame) noexcept {
        // Tracking retains only its own bounded grayscale history, not a source
        // color/GPU lease that could delay capture slot reuse.
        previous_.gray=frame.gray; previous_.source_ms=frame.source_ms;
        previous_.sequence=frame.sequence; previous_.generation=frame.generation;
        have_previous_=true;
    }

    TargetTrackerConfig config_{};
    SmallFrame previous_{};
    bool have_previous_{};
    Track enemy_{}, wolf_{};
    std::uint64_t next_id_{1}, lineage_{}, lineage_enemy_{}, lineage_wolf_{};
};

inline double TargetTracker::overlap(const TargetBox& a, const TargetBox& b) noexcept {
    const double area_a = std::max(0.0, a.right-a.left) * std::max(0.0, a.bottom-a.top);
    const double area_b = std::max(0.0, b.right-b.left) * std::max(0.0, b.bottom-b.top);
    const double intersection = std::max(0.0, std::min(a.right,b.right)-std::max(a.left,b.left)) *
        std::max(0.0, std::min(a.bottom,b.bottom)-std::max(a.top,b.top));
    return intersection / std::max(1e-9, area_a+area_b-intersection);
}

inline TargetBox TargetTracker::translated(TargetBox box, double dx, double dy) noexcept {
    dx = std::clamp(dx, -box.left, 1.0-box.right);
    dy = std::clamp(dy, -box.top, 1.0-box.bottom);
    box.left += dx; box.right += dx; box.top += dy; box.bottom += dy;
    return box;
}

inline CombatRoi TargetTracker::combat_roi(const TargetBox& enemy, const TargetBox* wolf) noexcept {
    TargetBox bounds = enemy;
    if (wolf) {
        bounds.left = std::min(bounds.left, wolf->left); bounds.right = std::max(bounds.right, wolf->right);
        bounds.top = std::min(bounds.top, wolf->top); bounds.bottom = std::max(bounds.bottom, wolf->bottom);
    }
    const double width = std::max(0.24, (bounds.right-bounds.left) * 1.45);
    const double height = std::max(0.25, (bounds.bottom-bounds.top) * 1.30);
    const double cx = bounds.center_x(), cy = bounds.center_y();
    const double w = std::min(width, 0.94), h = std::min(height, 0.89);
    const double left = std::clamp(cx-w/2, 0.03, 0.97-w);
    const double top = std::clamp(cy-h/2, 0.03, 0.92-h);
    return {left, top, std::min(0.97,left+w), std::min(0.92,top+h)};
}

inline TargetTracker::CameraMotion TargetTracker::camera_motion(const SmallFrame& frame) const {
    CameraMotion result;
    struct Point { int x, y; };
    std::array<Point, 800> points{};
    std::size_t count = 0;
    double sum = 0, square = 0;
    for (int y=14; y<112; y+=6) for (int x=12; x<244; x+=6) {
        const double nx = static_cast<double>(x)/vision_width, ny = static_cast<double>(y)/vision_height;
        const auto inside = [&](const Track& track) {
            return track.exists && nx > track.box.left-0.03 && nx < track.box.right+0.03 &&
                ny > track.box.top-0.03 && ny < track.box.bottom+0.03;
        };
        if (inside(enemy_) || inside(wolf_) || (ny>0.61 && nx>0.34 && nx<0.66)) continue;
        points[count++] = {x,y};
        const double value = pixel(frame,x,y); sum += value; square += value*value;
    }
    if (count < 60) return result;
    const double deviation = std::sqrt(std::max(0.0, square/count-(sum/count)*(sum/count)));
    if (deviation < 9) { result.cut = true; return result; }
    double best = std::numeric_limits<double>::infinity();
    for (int dy=-6; dy<=6; ++dy) for (int dx=-6; dx<=6; ++dx) {
        double error = 0;
        for (std::size_t i=0; i<count; ++i) {
            const auto p=points[i];
            error += std::min(30, std::abs(pixel(frame,p.x,p.y)-pixel(previous_,p.x-dx,p.y-dy)));
        }
        error = error/count + 0.025*(std::abs(dx)+std::abs(dy));
        if (error < best) { best=error; result.dx=dx; result.dy=dy; }
    }
    double absolute = 0;
    for (std::size_t i=0; i<count; ++i) {
        const auto p=points[i];
        const double delta = pixel(frame,p.x,p.y)-pixel(previous_,p.x-result.dx,p.y-result.dy);
        result.brightness += delta; absolute += std::min(40.0,std::abs(delta));
    }
    result.error = absolute/count; result.brightness /= count;
    result.cut = result.error > 21 || std::abs(result.brightness) > 24 ||
        std::abs(result.dx) == 6 || std::abs(result.dy) == 6;
    result.reliable = !result.cut && result.error < 16;
    return result;
}

inline std::vector<TargetTracker::Region> TargetTracker::motion_regions(
    const SmallFrame& frame, const CameraMotion& camera) const {
    constexpr int cell = 4, cols = vision_width/cell, rows = vision_height/cell;
    std::array<unsigned char, cols*rows> active{}, joined{}, visited{};
    const double threshold = std::max(15.0, camera.error*2.2+6);
    int total = 0;
    for (int cy=3; cy<29; ++cy) for (int cx=3; cx<61; ++cx) {
        // This is only an explicit fallback screen prior, not Wolf detection.
        if (cy>=23 && cx>=22 && cx<=42) continue;
        int changed = 0;
        for (int py=0; py<cell; py+=2) for (int px=0; px<cell; px+=2) {
            const int x=cx*cell+px, y=cy*cell+py;
            const double difference=std::abs(pixel(frame,x,y)-pixel(previous_,x-camera.dx,y-camera.dy)-camera.brightness);
            if (difference>threshold) ++changed;
        }
        if (changed>=2) { active[static_cast<std::size_t>(cy*cols+cx)]=1; ++total; }
    }
    // Large residual fields are camera/parallax/scene uncertainty, not a target.
    if (total < 5 || total > 420) return {};
    for (int y=2; y<rows-2; ++y) for (int x=2; x<cols-2; ++x) {
        if (!active[static_cast<std::size_t>(y*cols+x)]) continue;
        for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx)
            joined[static_cast<std::size_t>((y+dy)*cols+x+dx)]=1;
    }
    std::vector<Region> regions;
    std::array<int, cols*rows> queue{};
    for (int y=1; y<rows-1; ++y) for (int x=1; x<cols-1; ++x) {
        const int seed=y*cols+x;
        if (!joined[static_cast<std::size_t>(seed)] || visited[static_cast<std::size_t>(seed)]) continue;
        int head=0, tail=0, mass=0, l=cols, t=rows, r=0, b=0;
        queue[static_cast<std::size_t>(tail++)]=seed; visited[static_cast<std::size_t>(seed)]=1;
        while (head<tail) {
            const int at=queue[static_cast<std::size_t>(head++)], px=at%cols, py=at/cols;
            if (active[static_cast<std::size_t>(at)]) {
                ++mass; l=std::min(l,px); r=std::max(r,px+1); t=std::min(t,py); b=std::max(b,py+1);
            }
            for (int dy=-1; dy<=1; ++dy) for (int dx=-1; dx<=1; ++dx) {
                const int nx=px+dx, ny=py+dy;
                if (nx<1 || nx>=cols-1 || ny<1 || ny>=rows-1) continue;
                const int next=ny*cols+nx;
                if (joined[static_cast<std::size_t>(next)] && !visited[static_cast<std::size_t>(next)]) {
                    visited[static_cast<std::size_t>(next)]=1; queue[static_cast<std::size_t>(tail++)]=next;
                }
            }
        }
        if (mass<5 || r-l<3 || b-t<3 || (r-l)*(b-t)>400) continue;
        TargetBox box{static_cast<double>(l*cell)/vision_width,static_cast<double>(t*cell)/vision_height,
            static_cast<double>(r*cell)/vision_width,static_cast<double>(b*cell)/vision_height};
        if (!box.valid()) continue;
        const double density=static_cast<double>(mass)/((r-l)*(b-t));
        const double confidence=std::clamp(0.35+0.3*std::min(mass/30.0,1.0)+0.25*density,0.0,0.9);
        regions.push_back({box,static_cast<double>(mass),confidence});
    }
    return regions;
}

inline bool TargetTracker::follow_patch(const SmallFrame& frame, const CameraMotion& camera,
    TargetBox& tracked, double& quality) const {
    if (!enemy_.exists || !enemy_.box.valid()) return false;
    std::array<std::array<int,3>,36> samples{};
    double mean=0, square=0;
    int count=0;
    for (int y=0; y<6; ++y) for (int x=0; x<6; ++x) {
        const int px=static_cast<int>((enemy_.box.left+(x+0.5)/6*(enemy_.box.right-enemy_.box.left))*vision_width);
        const int py=static_cast<int>((enemy_.box.top+(y+0.5)/6*(enemy_.box.bottom-enemy_.box.top))*vision_height);
        if (px<12 || px>=vision_width-12 || py<12 || py>=vision_height-12) continue;
        const int value=pixel(previous_,px,py);
        samples[static_cast<std::size_t>(count++)]={px,py,value}; mean+=value; square+=value*value;
    }
    if (count<18 || std::sqrt(std::max(0.0,square/count-(mean/count)*(mean/count)))<15) return false;
    double best=1e9;
    int best_x=0,best_y=0;
    for (int dy=-5; dy<=5; ++dy) for (int dx=-5; dx<=5; ++dx) {
        double error=0;
        for (int i=0; i<count; ++i) {
            const auto p=samples[static_cast<std::size_t>(i)];
            error+=std::min(60.0,std::abs(pixel(frame,p[0]+camera.dx+dx,p[1]+camera.dy+dy)-p[2]-camera.brightness));
        }
        error=error/count+0.15*(std::abs(dx)+std::abs(dy));
        if (error<best) { best=error; best_x=dx; best_y=dy; }
    }
    if (best>15 || std::abs(best_x)==5 || std::abs(best_y)==5) return false;
    tracked=translated(enemy_.box,static_cast<double>(camera.dx+best_x)/vision_width,
        static_cast<double>(camera.dy+best_y)/vision_height);
    quality=std::clamp(1-best/25.0,0.0,1.0);
    return tracked.valid();
}

inline void TargetTracker::observe(Track& track, const TargetBox& box, double confidence,
    double time, bool semantic, std::uint64_t external_id) {
    if (!track.exists || track.semantic!=semantic ||
        (track.external_id && external_id && track.external_id!=external_id)) {
        track={}; track.id=next_id_++; track.first_ms=time; track.exists=true;
    }
    if (track.first_ms<=0) track.first_ms=time;
    track.box=box; track.confidence=confidence; track.last_seen_ms=time;
    track.semantic=semantic;
    if (external_id) track.external_id=external_id;
    track.observed=true;
    const double dwell=semantic ? config_.semantic_confirmation_ms : config_.visual_confirmation_ms;
    track.confirmed=time-track.first_ms>=dwell;
}

inline bool TargetTracker::update_semantic(Track& track, TargetRole role, const SmallFrame& frame,
    std::span<const TargetDetection> detections, bool& ambiguous) {
    const TargetDetection* chosen=nullptr;
    double best=-1, second=-1;
    for (const auto& detection : detections.first(std::min<std::size_t>(detections.size(),64))) {
        if (detection.role!=role || !detection.semantic_supported || !detection.box.valid() ||
            !std::isfinite(detection.confidence) || detection.confidence<config_.semantic_confidence || detection.confidence>1 ||
            !std::isfinite(detection.source_ms) || std::abs(detection.source_ms-frame.source_ms)>0.5 ||
            detection.generation!=frame.generation || detection.sequence!=frame.sequence) continue;
        double association=0;
        if (track.exists && track.semantic) {
            if (track.external_id && detection.detector_track_id && track.external_id!=detection.detector_track_id) continue;
            association=overlap(track.box,detection.box);
            if (association<0.15) continue;
        }
        const double distance=std::hypot(detection.box.center_x()-0.5,detection.box.center_y()-(role==TargetRole::Wolf?0.7:0.4));
        const double score=detection.confidence+0.45*association-0.2*distance;
        if (score>best) { second=best; best=score; chosen=&detection; }
        else second=std::max(second,score);
    }
    if (!chosen) return false;
    if (second>=0 && best-second<0.055) { ambiguous=true; return false; }
    observe(track,chosen->box,chosen->confidence,frame.source_ms,true,chosen->detector_track_id);
    return true;
}

inline TargetObservation TargetTracker::observation(const Track& track, double time) noexcept {
    TargetObservation out;
    if (!track.exists) return out;
    out.box=track.box; out.valid=track.confirmed && track.box.valid(); out.track_id=track.id;
    out.semantic_confirmed=track.semantic && track.confirmed;
    out.age_ms=std::max(0.0,time-track.last_seen_ms); out.observed_this_frame=track.observed;
    out.confidence=track.confidence*std::exp(-out.age_ms/180.0);
    return out;
}

inline TargetState TargetTracker::process(const SmallFrame& frame, std::span<const TargetDetection> detections) {
    TargetState result; result.source_ms=frame.source_ms; result.sequence=frame.sequence; result.generation=frame.generation;
    if (!std::isfinite(frame.source_ms) || frame.source_ms<=0 || !frame.sequence || !frame.generation) {
        result.reason="TARGET_INVALID_FRAME"; return result;
    }
    if (have_previous_ && frame.generation==previous_.generation &&
        (frame.source_ms<=previous_.source_ms || frame.sequence<=previous_.sequence)) {
        result.reason="TARGET_OUT_OF_ORDER"; return result;
    }
    if (have_previous_ && (frame.generation!=previous_.generation || frame.source_ms-previous_.source_ms>config_.max_gap_ms)) reset();
    CameraMotion camera;
    if (have_previous_) camera=camera_motion(frame);
    if (camera.cut) {
        enemy_={}; wolf_={}; result.reason="TARGET_CAMERA_CUT_OR_LOW_TEXTURE";
        remember(frame); return result;
    }
    result.camera_dx=camera.dx; result.camera_dy=camera.dy; result.camera_error=camera.error;
    result.camera_reliable=camera.reliable;
    const auto expire=[&](Track& track) {
        track.observed=false;
        if (track.exists && frame.source_ms-track.last_seen_ms>config_.hold_ms) track={};
    };
    expire(enemy_); expire(wolf_);
    bool ambiguous=false;
    const bool semantic_enemy=update_semantic(enemy_,TargetRole::Enemy,frame,detections,ambiguous);
    update_semantic(wolf_,TargetRole::Wolf,frame,detections,ambiguous);

    if (!semantic_enemy && (!enemy_.exists || !enemy_.semantic) && config_.allow_provisional &&
        have_previous_ && camera.reliable && !ambiguous) {
        TargetBox followed; double quality=0;
        const bool patch_ok=follow_patch(frame,camera,followed,quality);
        const auto regions=motion_regions(frame,camera);
        const Region* selected=nullptr;
        double best=-1, second=-1;
        for (const auto& region : regions) {
            const TargetBox predicted=patch_ok ? followed : translated(enemy_.box,
                static_cast<double>(camera.dx)/vision_width,static_cast<double>(camera.dy)/vision_height);
            const double continuity=enemy_.exists ? overlap(predicted,region.box) : 0;
            if (enemy_.exists && continuity<0.10) continue;
            const double distance=std::hypot(region.box.center_x()-0.5,region.box.center_y()-0.38);
            const double score=region.confidence+0.8*continuity-0.35*distance;
            if (score>best) { second=best; best=score; selected=&region; }
            else second=std::max(second,score);
        }
        if (selected && (second<0 || best-second>=0.07)) {
            // A successful patch match preserves box extent when residual motion
            // covers just an arm/edge, avoiding crop zoom from frame differencing.
            observe(enemy_,patch_ok?followed:selected->box,selected->confidence,frame.source_ms,false);
        } else if (patch_ok && second<0) {
            observe(enemy_,followed,std::min(enemy_.confidence,quality),frame.source_ms,false);
        } else if (second>=0) ambiguous=true;
    }
    if (!enemy_.observed && enemy_.exists && camera.reliable)
        enemy_.box=translated(enemy_.box,static_cast<double>(camera.dx)/vision_width,static_cast<double>(camera.dy)/vision_height);
    if (!wolf_.observed && wolf_.exists && camera.reliable)
        wolf_.box=translated(wolf_.box,static_cast<double>(camera.dx)/vision_width,static_cast<double>(camera.dy)/vision_height);
    if (!enemy_.observed && !enemy_.confirmed) enemy_.first_ms=0;
    if (!wolf_.observed && !wolf_.confirmed) wolf_.first_ms=0;
    if (enemy_.id!=lineage_enemy_ || wolf_.id!=lineage_wolf_) {
        lineage_enemy_=enemy_.id; lineage_wolf_=wolf_.id;
        lineage_=enemy_.exists?next_id_++:0;
    }
    result.enemy=observation(enemy_,frame.source_ms); result.wolf=observation(wolf_,frame.source_ms);
    result.track_lineage=lineage_; result.held=enemy_.exists && !enemy_.observed;
    result.valid=result.enemy.valid && result.enemy.observed_this_frame && !ambiguous;
    result.identity_certain=result.valid && result.enemy.semantic_confirmed && result.wolf.valid &&
        result.wolf.semantic_confirmed && result.wolf.observed_this_frame && overlap(result.enemy.box,result.wolf.box)<0.65;
    if (result.enemy.valid) result.roi=combat_roi(result.enemy.box,result.wolf.valid?&result.wolf.box:nullptr);
    if (ambiguous) result.reason="TARGET_AMBIGUOUS";
    else if (result.identity_certain) result.reason="SEMANTIC_WOLF_ENEMY_TRACKED";
    else if (result.valid && !result.enemy.semantic_confirmed) result.reason="PROVISIONAL_VISUAL_REGION_NO_SEMANTIC_IDENTITY";
    else if (result.valid) result.reason="WOLF_IDENTITY_UNCONFIRMED";
    else if (result.held) result.reason="TARGET_HELD_NO_CURRENT_EVIDENCE";
    else if (enemy_.exists) result.reason="TARGET_CONFIRMING";
    else result.reason=have_previous_?"NO_TARGET_EVIDENCE":"TARGET_WARMUP";
    remember(frame); return result;
}
}
