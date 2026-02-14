#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <locale>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifdef _WIN32
#include <windows.h>
#undef ERROR  // Windows.h 定义了 ERROR 宏，与 LogLevel::ERROR 冲突
#endif

using namespace celegans;

struct BehaviorEvent {
    double time_ms;
    std::string type;  // "reversal_start", "reversal_end", "omega_start", etc.
    double value;      // 持续时间或其他指标
};

struct TrajectoryPoint {
    double time_ms;
    Vector2d position;
    double heading;
    double speed;
    double curvature;
};

// 互斥运动状态
enum class MotionState { FORWARD, REVERSE, OMEGA, PAUSE };

struct BoutInfo {
    double start_ms;
    double duration_ms;
    double turn_angle_deg;  // 状态结束时的累计转角
};

// === Event Trace Structs (--trace-events) ===
struct OmegaTrace {
    int index = 0;
    double start_s = 0;
    double end_s = 0;
    double heading_start_deg = 0;
    double heading_end_deg = 0;
    double turn_angle_deg = 0;
    double direction_at_start = 0;
    double direction_at_end = 0;
    double rivl_release = 0;
    double rivr_release = 0;
    double peak_curvature = 0;
    double peak_speed = 0;
    double muscle_d_max = 0;  // peak dorsal head activation
    double muscle_v_max = 0;  // peak ventral head activation
    bool toward_food = false;
    double food_angle_deg = 0; // angle from heading to food at start
};

struct ReversalTrace {
    int index = 0;
    double start_s = 0;
    double end_s = 0;
    double heading_start_deg = 0;
    double heading_end_deg = 0;
    double turn_angle_deg = 0;
    double speed_at_start = 0;
    double food_dist_start = 0;
    double food_dist_end = 0;
    bool followed_by_omega = false;
};

struct BehaviorMetrics {
    // 运动学
    double mean_speed = 0;
    double std_speed = 0;
    double max_speed = 0;
    double total_distance = 0;
    double net_displacement = 0;
    double path_efficiency = 0;
    
    // 互斥运动状态时间占比 (总和 = 100%)
    double forward_time_pct = 0;
    double reverse_time_pct = 0;
    double omega_time_pct = 0;
    double pause_time_pct = 0;
    
    // Step 121: Roaming/Dwelling 觅食状态 (SimulationEngine 分类器)
    double roaming_time_pct = 0;
    double dwelling_time_pct = 0;
    int foraging_transitions = 0;    // R↔D 状态切换次数
    double mean_roaming_bout_s = 0;  // 平均 roaming 持续时间
    double mean_dwelling_bout_s = 0; // 平均 dwelling 持续时间
    
    // Bout 统计
    int forward_bout_count = 0;
    int reverse_bout_count = 0;
    int omega_bout_count = 0;
    double mean_forward_bout_s = 0;
    double std_forward_bout_s = 0;
    double mean_reverse_bout_s = 0;
    double std_reverse_bout_s = 0;
    double mean_omega_bout_s = 0;
    
    // 转角
    double mean_reversal_turn_deg = 0;
    double mean_omega_turn_deg = 0;
    
    // 头部摆动
    double mean_head_curvature = 0;
    double head_curvature_std = 0;
    double head_swing_frequency = 0;
    
    // 趋化性
    double chemotaxis_index = 0;
    double time_near_target_pct = 0;
    double mean_distance_to_target = 0;
    double directional_persistence = 0;
    double mean_approach_speed = 0;
    double mean_retreat_speed = 0;
};

class BehaviorAnalyzer {
public:
    BehaviorAnalyzer(double duration_s, unsigned int seed, Vector2d target, bool trace_events = false)
        : duration_s_(duration_s), seed_(seed), target_(target), trace_events_(trace_events) {}
    
    void run() {
        std::cout << "  运行仿真... " << std::flush;
        
        SimulationEngine sim;
        sim.initialize_default();
        sim.set_rng_seed(seed_);
        
        // 环境设置
        sim.environment().chemical_field().clear();
        sim.environment().chemical_field().add_point_source(target_, 1.0);
        sim.environment().soluble_field().add_point_source(target_, 0.4);
        sim.reset_transducers();
        
        // 仿真参数
        double duration_ms = duration_s_ * 1000.0;
        int total_steps = (int)(duration_ms / sim.dt());
        int sample_interval = (int)(50.0 / sim.dt());  // 50ms 采样
        double dt_sample = 50.0;  // ms
        
        // Trace: find RIVL/RIVR neuron indices
        int rivl_idx = -1, rivr_idx = -1;
        if (trace_events_) {
            for (int i = 0; i < (int)sim.neurons().size(); ++i) {
                if (sim.neurons()[i]->name() == "RIVL") rivl_idx = i;
                if (sim.neurons()[i]->name() == "RIVR") rivr_idx = i;
            }
        }
        
        // 互斥状态机
        MotionState curr_state = MotionState::FORWARD;
        MotionState prev_state = MotionState::FORWARD;
        double state_start_time = 0;
        double state_start_heading = 0;
        
        // 互斥状态时间累计
        double forward_time = 0, reverse_time = 0, omega_time = 0, pause_time = 0;
        double roaming_time = 0, dwelling_time = 0;
        double near_target_time = 0;

        // Step 121: Foraging state bout tracking
        using FS = SimulationEngine::ForagingState;
        FS prev_foraging = FS::DWELLING;
        double foraging_bout_start = 0;
        std::vector<double> roaming_bouts, dwelling_bouts;
        int foraging_trans = 0;
        
        // Trace: active event tracking
        OmegaTrace active_omega;
        ReversalTrace active_reversal;
        bool in_omega_trace = false;
        bool in_reversal_trace = false;
        int omega_trace_count = 0;
        int reversal_trace_count = 0;
        
        for (int s = 0; s < total_steps; ++s) {
            sim.step();
            double t = sim.current_time();
            
            if ((s + 1) % sample_interval == 0) {
                // 记录轨迹
                TrajectoryPoint pt;
                pt.time_ms = t;
                pt.position = sim.body().get_head_position();
                pt.heading = sim.body().get_head_angle() * 180.0 / M_PI;  // rad -> deg
                pt.speed = sim.body().get_speed();
                pt.curvature = sim.body().segments()[0].curvature;
                trajectory_.push_back(pt);
                
                // 互斥状态机判定: OMEGA > REVERSE > FORWARD (优先级)
                bool is_rev = sim.is_reversing();
                bool is_omega = sim.is_omega_turning();
                
                if (is_omega) {
                    curr_state = MotionState::OMEGA;
                } else if (is_rev) {
                    curr_state = MotionState::REVERSE;
                } else if (pt.speed > 0.01) {
                    curr_state = MotionState::FORWARD;
                } else {
                    curr_state = MotionState::PAUSE;
                }
                
                // === Trace: collect per-sample data during active events ===
                if (trace_events_) {
                    // Omega trace: collect peak values each sample
                    if (in_omega_trace && is_omega) {
                        double curv = std::abs(pt.curvature);
                        if (curv > active_omega.peak_curvature) active_omega.peak_curvature = curv;
                        if (pt.speed > active_omega.peak_speed) active_omega.peak_speed = pt.speed;
                        double d_act = sim.body().muscles().get_dorsal_activation(0);
                        double v_act = sim.body().muscles().get_ventral_activation(0);
                        if (d_act > active_omega.muscle_d_max) active_omega.muscle_d_max = d_act;
                        if (v_act > active_omega.muscle_v_max) active_omega.muscle_v_max = v_act;
                        active_omega.direction_at_end = sim.body().get_direction();
                    }
                    
                    // Reversal trace: update end heading
                    if (in_reversal_trace && is_rev) {
                        // tracked at end below
                    }
                    
                    // Omega START
                    if (is_omega && !in_omega_trace) {
                        in_omega_trace = true;
                        active_omega = OmegaTrace{};
                        active_omega.index = ++omega_trace_count;
                        active_omega.start_s = t / 1000.0;
                        active_omega.heading_start_deg = pt.heading;
                        active_omega.direction_at_start = sim.body().get_direction();
                        if (rivl_idx >= 0) active_omega.rivl_release = sim.neurons()[rivl_idx]->get_transmitter_release_rate();
                        if (rivr_idx >= 0) active_omega.rivr_release = sim.neurons()[rivr_idx]->get_transmitter_release_rate();
                        active_omega.peak_curvature = std::abs(pt.curvature);
                        active_omega.peak_speed = pt.speed;
                        active_omega.muscle_d_max = sim.body().muscles().get_dorsal_activation(0);
                        active_omega.muscle_v_max = sim.body().muscles().get_ventral_activation(0);
                        // Food direction relative to heading
                        double to_food_x = target_.x - pt.position.x;
                        double to_food_y = target_.y - pt.position.y;
                        double food_angle = std::atan2(to_food_y, to_food_x) * 180.0 / M_PI;
                        active_omega.food_angle_deg = food_angle - pt.heading;
                        while (active_omega.food_angle_deg > 180.0) active_omega.food_angle_deg -= 360.0;
                        while (active_omega.food_angle_deg < -180.0) active_omega.food_angle_deg += 360.0;
                    }
                    
                    // Omega END
                    if (!is_omega && in_omega_trace) {
                        in_omega_trace = false;
                        active_omega.end_s = t / 1000.0;
                        active_omega.heading_end_deg = pt.heading;
                        double ta = active_omega.heading_end_deg - active_omega.heading_start_deg;
                        while (ta > 180.0) ta -= 360.0;
                        while (ta < -180.0) ta += 360.0;
                        active_omega.turn_angle_deg = ta;
                        // Toward food: did the turn reduce the angle to food?
                        double food_angle_after = active_omega.food_angle_deg - ta;
                        while (food_angle_after > 180.0) food_angle_after -= 360.0;
                        while (food_angle_after < -180.0) food_angle_after += 360.0;
                        active_omega.toward_food = (std::abs(food_angle_after) < std::abs(active_omega.food_angle_deg));
                        omega_traces_.push_back(active_omega);
                    }
                    
                    // Reversal START
                    if (is_rev && !in_reversal_trace) {
                        in_reversal_trace = true;
                        active_reversal = ReversalTrace{};
                        active_reversal.index = ++reversal_trace_count;
                        active_reversal.start_s = t / 1000.0;
                        active_reversal.heading_start_deg = pt.heading;
                        active_reversal.speed_at_start = pt.speed;
                        double ddx = pt.position.x - target_.x;
                        double ddy = pt.position.y - target_.y;
                        active_reversal.food_dist_start = std::sqrt(ddx*ddx + ddy*ddy);
                    }
                    
                    // Reversal END
                    if (!is_rev && in_reversal_trace) {
                        in_reversal_trace = false;
                        active_reversal.end_s = t / 1000.0;
                        active_reversal.heading_end_deg = pt.heading;
                        double ta = active_reversal.heading_end_deg - active_reversal.heading_start_deg;
                        while (ta > 180.0) ta -= 360.0;
                        while (ta < -180.0) ta += 360.0;
                        active_reversal.turn_angle_deg = ta;
                        double ddx = pt.position.x - target_.x;
                        double ddy = pt.position.y - target_.y;
                        active_reversal.food_dist_end = std::sqrt(ddx*ddx + ddy*ddy);
                        active_reversal.followed_by_omega = is_omega;
                        reversal_traces_.push_back(active_reversal);
                    }
                }
                
                // 状态转换: 记录 bout
                if (curr_state != prev_state) {
                    double bout_dur = t - state_start_time;
                    double turn_angle = pt.heading - state_start_heading;
                    // 归一化到 [-180, 180]
                    while (turn_angle > 180.0) turn_angle -= 360.0;
                    while (turn_angle < -180.0) turn_angle += 360.0;
                    
                    if (bout_dur > 0 && state_start_time > 0) {
                        BoutInfo bout;
                        bout.start_ms = state_start_time;
                        bout.duration_ms = bout_dur;
                        bout.turn_angle_deg = turn_angle;
                        
                        if (prev_state == MotionState::FORWARD) {
                            forward_bouts_.push_back(bout);
                        } else if (prev_state == MotionState::REVERSE) {
                            reverse_bouts_.push_back(bout);
                        } else if (prev_state == MotionState::OMEGA) {
                            omega_bouts_.push_back(bout);
                        }
                        
                        // 记录事件
                        BehaviorEvent evt;
                        evt.time_ms = t;
                        if (prev_state == MotionState::REVERSE) {
                            evt.type = "REV";
                            evt.value = bout_dur;
                        } else if (prev_state == MotionState::OMEGA) {
                            evt.type = "OMEGA";
                            evt.value = bout_dur;
                        } else {
                            evt.type = "FWD";
                            evt.value = bout_dur;
                        }
                        events_.push_back(evt);
                    }
                    
                    state_start_time = t;
                    state_start_heading = pt.heading;
                    prev_state = curr_state;
                }
                
                // 互斥状态时间累计 (总和 = 100%)
                switch (curr_state) {
                    case MotionState::FORWARD: forward_time += dt_sample; break;
                    case MotionState::REVERSE: reverse_time += dt_sample; break;
                    case MotionState::OMEGA:   omega_time += dt_sample; break;
                    case MotionState::PAUSE:   pause_time += dt_sample; break;
                }
                
                // Step 121: Foraging state from SimulationEngine classifier
                FS cur_foraging = sim.foraging_state();
                if (cur_foraging == FS::ROAMING) {
                    roaming_time += dt_sample;
                } else {
                    dwelling_time += dt_sample;
                }
                // Track foraging state transitions and bout durations
                if (cur_foraging != prev_foraging && t > 3000.0) {
                    double bout_dur = (t - foraging_bout_start) / 1000.0;
                    if (bout_dur > 0.5) { // filter very short transients
                        if (prev_foraging == FS::ROAMING) roaming_bouts.push_back(bout_dur);
                        else dwelling_bouts.push_back(bout_dur);
                    }
                    foraging_trans++;
                    foraging_bout_start = t;
                }
                prev_foraging = cur_foraging;
                
                // 目标距离
                double dx = pt.position.x - target_.x;
                double dy = pt.position.y - target_.y;
                double dist = std::sqrt(dx*dx + dy*dy);
                if (dist < 5.0) near_target_time += dt_sample;
            }
        }
        
        std::cout << "完成！\n\n";
        
        // 计算指标
        compute_metrics(forward_time, reverse_time, omega_time, pause_time,
                       roaming_time, dwelling_time, near_target_time,
                       foraging_trans, roaming_bouts, dwelling_bouts);
    }
    
    const BehaviorMetrics& metrics() const { return metrics_; }
    const std::vector<BehaviorEvent>& events() const { return events_; }
    const std::vector<TrajectoryPoint>& trajectory() const { return trajectory_; }
    const std::vector<OmegaTrace>& omega_traces() const { return omega_traces_; }
    const std::vector<ReversalTrace>& reversal_traces() const { return reversal_traces_; }
    
    void export_trajectory_csv(const std::string& filename) const {
        std::ofstream ofs(filename);
        if (!ofs) return;
        
        ofs << "time_ms,x,y,heading,speed,curvature\n";
        for (const auto& pt : trajectory_) {
            ofs << std::fixed << std::setprecision(2)
                << pt.time_ms << ","
                << pt.position.x << "," << pt.position.y << ","
                << std::setprecision(1) << pt.heading << ","
                << std::setprecision(3) << pt.speed << ","
                << std::setprecision(4) << pt.curvature << "\n";
        }
    }
    
    void export_events_csv(const std::string& filename) const {
        std::ofstream ofs(filename);
        if (!ofs) return;
        
        ofs << "time_ms,event_type,duration_ms\n";
        for (const auto& evt : events_) {
            ofs << std::fixed << std::setprecision(2)
                << evt.time_ms << "," << evt.type << "," << evt.value << "\n";
        }
    }
    
private:
    double duration_s_;
    unsigned int seed_;
    Vector2d target_;
    bool trace_events_;
    
    std::vector<TrajectoryPoint> trajectory_;
    std::vector<BehaviorEvent> events_;
    std::vector<BoutInfo> forward_bouts_;
    std::vector<BoutInfo> reverse_bouts_;
    std::vector<BoutInfo> omega_bouts_;
    std::vector<OmegaTrace> omega_traces_;
    std::vector<ReversalTrace> reversal_traces_;
    BehaviorMetrics metrics_;
    
    static double bout_mean(const std::vector<BoutInfo>& bouts) {
        if (bouts.empty()) return 0;
        double sum = 0;
        for (const auto& b : bouts) sum += b.duration_ms;
        return (sum / bouts.size()) / 1000.0;  // ms -> s
    }
    
    static double bout_std(const std::vector<BoutInfo>& bouts, double mean_s) {
        if (bouts.size() < 2) return 0;
        double sq_sum = 0;
        for (const auto& b : bouts) {
            double d = b.duration_ms / 1000.0 - mean_s;
            sq_sum += d * d;
        }
        return std::sqrt(sq_sum / (bouts.size() - 1));
    }
    
    static double bout_mean_turn(const std::vector<BoutInfo>& bouts) {
        if (bouts.empty()) return 0;
        double sum = 0;
        for (const auto& b : bouts) sum += std::abs(b.turn_angle_deg);
        return sum / bouts.size();
    }
    
    void compute_metrics(double forward_time, double reverse_time, double omega_time, double pause_time,
                        double roaming_time, double dwelling_time, double near_target_time,
                        int foraging_trans = 0,
                        const std::vector<double>& roaming_bouts = {},
                        const std::vector<double>& dwelling_bouts = {}) {
        if (trajectory_.empty()) return;
        
        double total_time = duration_s_ * 1000.0;
        
        // === 运动学 ===
        std::vector<double> speeds;
        double total_dist = 0;
        for (size_t i = 0; i < trajectory_.size(); ++i) {
            speeds.push_back(trajectory_[i].speed);
            if (i > 0) {
                double dx = trajectory_[i].position.x - trajectory_[i-1].position.x;
                double dy = trajectory_[i].position.y - trajectory_[i-1].position.y;
                total_dist += std::sqrt(dx*dx + dy*dy);
            }
        }
        
        metrics_.mean_speed = std::accumulate(speeds.begin(), speeds.end(), 0.0) / speeds.size();
        if (speeds.size() > 1) {
            double sq_sum = 0;
            for (double s : speeds) sq_sum += (s - metrics_.mean_speed) * (s - metrics_.mean_speed);
            metrics_.std_speed = std::sqrt(sq_sum / (speeds.size() - 1));
        }
        metrics_.max_speed = *std::max_element(speeds.begin(), speeds.end());
        metrics_.total_distance = total_dist;
        
        auto& first = trajectory_.front();
        auto& last = trajectory_.back();
        double dx = last.position.x - first.position.x;
        double dy = last.position.y - first.position.y;
        metrics_.net_displacement = std::sqrt(dx*dx + dy*dy);
        metrics_.path_efficiency = (total_dist > 0.001) ? metrics_.net_displacement / total_dist : 0;
        
        // === 互斥运动状态 (总和 = 100%) ===
        double motion_total = forward_time + reverse_time + omega_time + pause_time;
        if (motion_total > 0) {
            metrics_.forward_time_pct = 100.0 * forward_time / motion_total;
            metrics_.reverse_time_pct = 100.0 * reverse_time / motion_total;
            metrics_.omega_time_pct = 100.0 * omega_time / motion_total;
            metrics_.pause_time_pct = 100.0 * pause_time / motion_total;
        }
        
        // === Step 121: Roaming/Dwelling 觅食状态 (SimulationEngine 分类器) ===
        double neuromod_total = roaming_time + dwelling_time;
        if (neuromod_total > 0) {
            metrics_.roaming_time_pct = 100.0 * roaming_time / neuromod_total;
            metrics_.dwelling_time_pct = 100.0 * dwelling_time / neuromod_total;
        }
        metrics_.foraging_transitions = foraging_trans;
        if (!roaming_bouts.empty()) {
            double sum = 0; for (double b : roaming_bouts) sum += b;
            metrics_.mean_roaming_bout_s = sum / roaming_bouts.size();
        }
        if (!dwelling_bouts.empty()) {
            double sum = 0; for (double b : dwelling_bouts) sum += b;
            metrics_.mean_dwelling_bout_s = sum / dwelling_bouts.size();
        }
        
        // === Bout 统计 ===
        metrics_.forward_bout_count = (int)forward_bouts_.size();
        metrics_.reverse_bout_count = (int)reverse_bouts_.size();
        metrics_.omega_bout_count = (int)omega_bouts_.size();
        
        metrics_.mean_forward_bout_s = bout_mean(forward_bouts_);
        metrics_.std_forward_bout_s = bout_std(forward_bouts_, metrics_.mean_forward_bout_s);
        metrics_.mean_reverse_bout_s = bout_mean(reverse_bouts_);
        metrics_.std_reverse_bout_s = bout_std(reverse_bouts_, metrics_.mean_reverse_bout_s);
        metrics_.mean_omega_bout_s = bout_mean(omega_bouts_);
        
        // === 转角 ===
        metrics_.mean_reversal_turn_deg = bout_mean_turn(reverse_bouts_);
        metrics_.mean_omega_turn_deg = bout_mean_turn(omega_bouts_);
        
        // === 头部摆动 (曲率统计) ===
        std::vector<double> curvatures;
        for (const auto& pt : trajectory_) {
            curvatures.push_back(std::abs(pt.curvature));
        }
        if (!curvatures.empty()) {
            metrics_.mean_head_curvature = std::accumulate(curvatures.begin(), curvatures.end(), 0.0) / curvatures.size();
            double sq = 0;
            for (double c : curvatures) sq += (c - metrics_.mean_head_curvature) * (c - metrics_.mean_head_curvature);
            metrics_.head_curvature_std = std::sqrt(sq / curvatures.size());
        }
        // 摆动频率: 计算曲率零交叉数
        int zero_crossings = 0;
        for (size_t i = 1; i < trajectory_.size(); ++i) {
            if (trajectory_[i].curvature * trajectory_[i-1].curvature < 0) {
                zero_crossings++;
            }
        }
        double trace_duration_s = (trajectory_.back().time_ms - trajectory_.front().time_ms) / 1000.0;
        if (trace_duration_s > 0) {
            metrics_.head_swing_frequency = (zero_crossings / 2.0) / trace_duration_s;
        }
        
        // === 趋化性 ===
        double first_dist = std::sqrt((first.position.x - target_.x) * (first.position.x - target_.x) +
                                      (first.position.y - target_.y) * (first.position.y - target_.y));
        double final_dist = std::sqrt((last.position.x - target_.x) * (last.position.x - target_.x) +
                                      (last.position.y - target_.y) * (last.position.y - target_.y));
        metrics_.chemotaxis_index = (first_dist > 0.1) ? (first_dist - final_dist) / first_dist : 0;
        metrics_.time_near_target_pct = 100.0 * near_target_time / total_time;
        
        double sum_dist = 0;
        for (const auto& pt : trajectory_) {
            double ddx = pt.position.x - target_.x;
            double ddy = pt.position.y - target_.y;
            sum_dist += std::sqrt(ddx*ddx + ddy*ddy);
        }
        metrics_.mean_distance_to_target = sum_dist / trajectory_.size();
        
        // === 方向性 ===
        int toward = 0, away = 0;
        double approach_sum = 0, retreat_sum = 0;
        for (size_t i = 1; i < trajectory_.size(); ++i) {
            double pd = std::sqrt((trajectory_[i-1].position.x - target_.x) * (trajectory_[i-1].position.x - target_.x) +
                                  (trajectory_[i-1].position.y - target_.y) * (trajectory_[i-1].position.y - target_.y));
            double cd = std::sqrt((trajectory_[i].position.x - target_.x) * (trajectory_[i].position.x - target_.x) +
                                  (trajectory_[i].position.y - target_.y) * (trajectory_[i].position.y - target_.y));
            if (cd < pd) { toward++; approach_sum += trajectory_[i].speed; }
            else if (cd > pd) { away++; retreat_sum += trajectory_[i].speed; }
        }
        int tot = toward + away;
        metrics_.directional_persistence = (tot > 0) ? 100.0 * toward / tot : 0;
        metrics_.mean_approach_speed = (toward > 0) ? approach_sum / toward : 0;
        metrics_.mean_retreat_speed = (away > 0) ? retreat_sum / away : 0;
    }
};

void print_trace_events(const std::vector<OmegaTrace>& omegas, const std::vector<ReversalTrace>& reversals) {
    std::cout << "========================================\n";
    std::cout << "  EVENT TRACES\n";
    std::cout << "========================================\n\n";
    
    // --- Omega Traces ---
    if (!omegas.empty()) {
        std::cout << "--- Omega Turns (" << omegas.size() << ") ---\n\n";
        int toward_count = 0;
        for (const auto& o : omegas) {
            if (o.toward_food) toward_count++;
            std::cout << "  OMEGA #" << o.index << " at t=" << std::fixed << std::setprecision(2) << o.start_s << "s:\n";
            std::cout << "    heading:   " << std::setprecision(1) << o.heading_start_deg
                      << "° → " << o.heading_end_deg << "°  (Δ=" << std::showpos << o.turn_angle_deg
                      << std::noshowpos << "°, " << (o.toward_food ? "toward_food" : "away_food") << ")\n";
            std::cout << "    direction: " << std::setprecision(0) << o.direction_at_start
                      << " → " << o.direction_at_end << "\n";
            std::cout << "    RIVL=" << std::setprecision(3) << o.rivl_release
                      << "  RIVR=" << o.rivr_release
                      << "  (" << (o.rivl_release > o.rivr_release ? "L>R→ventral_bias" : "R>L→dorsal_bias") << ")\n";
            std::cout << "    peak_curv=" << std::setprecision(2) << o.peak_curvature << "/mm"
                      << "  peak_speed=" << std::setprecision(3) << o.peak_speed << "mm/s\n";
            std::cout << "    muscle D_max=" << std::setprecision(1) << o.muscle_d_max
                      << "  V_max=" << o.muscle_v_max
                      << "  food_angle=" << std::setprecision(0) << o.food_angle_deg << "°\n";
            std::cout << "    duration:  " << std::setprecision(0) << (o.end_s - o.start_s) * 1000.0 << "ms\n\n";
        }
        std::cout << "  → Omega toward food: " << toward_count << "/" << (int)omegas.size()
                  << " (" << std::setprecision(0) << (100.0 * toward_count / omegas.size()) << "%)\n\n";
    }
    
    // --- Reversal Traces ---
    if (!reversals.empty()) {
        std::cout << "--- Reversals (" << reversals.size() << ") ---\n\n";
        int omega_follow = 0;
        for (const auto& r : reversals) {
            if (r.followed_by_omega) omega_follow++;
            std::cout << "  REV #" << r.index << " at t=" << std::fixed << std::setprecision(2) << r.start_s << "s:";
            std::cout << "  dur=" << std::setprecision(0) << (r.end_s - r.start_s) * 1000.0 << "ms";
            std::cout << "  Δheading=" << std::showpos << std::setprecision(1) << r.turn_angle_deg << std::noshowpos << "°";
            std::cout << "  food_dist " << std::setprecision(1) << r.food_dist_start << "→" << r.food_dist_end << "mm";
            if (r.followed_by_omega) std::cout << "  →OMEGA";
            std::cout << "\n";
        }
        std::cout << "\n  → Followed by omega: " << omega_follow << "/" << (int)reversals.size()
                  << " (" << std::setprecision(0) << (100.0 * omega_follow / reversals.size()) << "%)\n\n";
    }
}

void print_metrics(const BehaviorMetrics& m, bool verbose) {
    std::cout << "========================================\n";
    std::cout << "  BEHAVIOR METRICS\n";
    std::cout << "========================================\n\n";
    
    std::cout << "--- 运动学 ---\n";
    std::cout << "  平均速度:           " << std::fixed << std::setprecision(3) 
              << m.mean_speed << " +/- " << m.std_speed << " mm/s\n";
    std::cout << "  最大速度:           " << m.max_speed << " mm/s\n";
    std::cout << "  总路程:             " << std::setprecision(1) << m.total_distance << " mm\n";
    std::cout << "  净位移:             " << m.net_displacement << " mm\n";
    std::cout << "  路径效率:           " << std::setprecision(3) << m.path_efficiency
              << "  (net/total)\n\n";
    
    std::cout << "--- 运动状态 (互斥, 总和=100%) ---\n";
    std::cout << "  Forward:            " << std::setprecision(1) << m.forward_time_pct << "%\n";
    std::cout << "  Reverse:            " << m.reverse_time_pct << "%\n";
    std::cout << "  Omega:              " << m.omega_time_pct << "%\n";
    std::cout << "  Pause:              " << m.pause_time_pct << "%\n\n";
    
    std::cout << "--- \u89c5\u98df\u72b6\u6001 (Step 121: speed+reversal \u53cc\u7a33\u6001\u5206\u7c7b\u5668) ---\n";
    std::cout << "  Roaming:            " << m.roaming_time_pct << "%\n";
    std::cout << "  Dwelling:           " << m.dwelling_time_pct << "%\n";
    std::cout << "  R\u2194D transitions:    " << m.foraging_transitions << "\n";
    if (m.mean_roaming_bout_s > 0)
        std::cout << "  Mean roaming bout:  " << std::setprecision(1) << m.mean_roaming_bout_s << " s\n";
    if (m.mean_dwelling_bout_s > 0)
        std::cout << "  Mean dwelling bout: " << std::setprecision(1) << m.mean_dwelling_bout_s << " s\n";
    std::cout << "\n";
    
    std::cout << "--- Bout 统计 ---\n";
    std::cout << "  Forward bouts:      " << m.forward_bout_count 
              << ",  均长 " << std::setprecision(2) << m.mean_forward_bout_s 
              << " +/- " << m.std_forward_bout_s << " s\n";
    std::cout << "  Reverse bouts:      " << m.reverse_bout_count 
              << ",  均长 " << m.mean_reverse_bout_s 
              << " +/- " << m.std_reverse_bout_s << " s\n";
    std::cout << "  Omega bouts:        " << m.omega_bout_count
              << ",  均长 " << m.mean_omega_bout_s << " s\n";
    
    if (m.reverse_bout_count > 0) {
        std::cout << "  Omega/Reversal:     " << std::setprecision(2) 
                  << (m.omega_bout_count > 0 ? (double)m.omega_bout_count / m.reverse_bout_count : 0.0) << "\n";
    }
    std::cout << "\n";
    
    std::cout << "--- 转角 ---\n";
    std::cout << "  反转后平均转角:     " << std::setprecision(1) << m.mean_reversal_turn_deg << " deg\n";
    std::cout << "  Omega平均转角:      " << m.mean_omega_turn_deg << " deg\n\n";
    
    std::cout << "--- 头部曲率 ---\n";
    std::cout << "  平均曲率:           " << std::setprecision(2) << m.mean_head_curvature << " /mm\n";
    std::cout << "  曲率标准差:         " << m.head_curvature_std << " /mm\n";
    std::cout << "  摆动频率:           " << m.head_swing_frequency << " Hz"
              << "  (曲率零交叉)\n\n";
    
    std::cout << "--- 趋化性 ---\n";
    std::cout << "  Chemotaxis Index:   " << std::setprecision(3) << m.chemotaxis_index << "\n";
    std::cout << "  目标附近时间:       " << std::setprecision(1) << m.time_near_target_pct << "%\n";
    std::cout << "  平均目标距离:       " << m.mean_distance_to_target << " mm\n";
    std::cout << "  方向持续性:         " << m.directional_persistence << "%\n";
    
    if (verbose) {
        std::cout << "  接近速度:           " << std::setprecision(3) 
                  << m.mean_approach_speed << " mm/s\n";
        std::cout << "  撤退速度:           " << m.mean_retreat_speed << " mm/s\n";
    }
    std::cout << "\n";
}

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    Logger::instance().set_level(LogLevel::ERROR);
    
    double duration = 60.0;
    unsigned int seed = 123;
    Vector2d target{35.0, 25.0};
    bool verbose = false;
    bool trace_events = false;
    std::string export_trajectory, export_events;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i+1 < argc) {
            duration = std::atof(argv[++i]);
        } else if (arg == "--seed" && i+1 < argc) {
            seed = std::atoi(argv[++i]);
        } else if (arg == "--target-x" && i+1 < argc) {
            target.x = std::atof(argv[++i]);
        } else if (arg == "--target-y" && i+1 < argc) {
            target.y = std::atof(argv[++i]);
        } else if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--trace-events" || arg == "-t") {
            trace_events = true;
        } else if (arg == "--export-trajectory" && i+1 < argc) {
            export_trajectory = argv[++i];
        } else if (arg == "--export-events" && i+1 < argc) {
            export_events = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: behavior_analyzer [options]\n\n"
                      << "科研级行为分析工具\n\n"
                      << "Options:\n"
                      << "  --duration <sec>           仿真时长 (默认: 60)\n"
                      << "  --seed <n>                 随机种子 (默认: 123)\n"
                      << "  --target-x <mm>            目标X坐标 (默认: 35)\n"
                      << "  --target-y <mm>            目标Y坐标 (默认: 25)\n"
                      << "  --verbose / -v             详细输出\n"
                      << "  --trace-events / -t        输出每个omega/reversal事件的详细轨迹\n"
                      << "  --export-trajectory <csv>  导出轨迹数据\n"
                      << "  --export-events <csv>      导出行为事件\n"
                      << "  --help / -h                显示帮助\n\n"
                      << "输出指标:\n"
                      << "  - 运动学: 速度、路径效率、净位移\n"
                      << "  - 行为事件: 反转、Omega转弯频率和时长\n"
                      << "  - 状态占比: Roaming/Dwelling/Reversing/Omega\n"
                      << "  - 趋化性: CI、目标距离、方向持续性\n"
                      << "  - 头部运动: 摆动幅度和频率\n"
                      << "  - --trace-events: 每个omega/reversal的详细诊断数据\n";
            return 0;
        }
    }
    
    std::cout << "========================================\n";
    std::cout << "  行为分析器 (科研级)\n";
    std::cout << "========================================\n\n";
    std::cout << "  仿真时长:  " << duration << " s\n";
    std::cout << "  随机种子:  " << seed << "\n";
    std::cout << "  目标位置:  (" << target.x << ", " << target.y << ")\n\n";
    
    BehaviorAnalyzer analyzer(duration, seed, target, trace_events);
    analyzer.run();
    
    print_metrics(analyzer.metrics(), verbose);
    
    if (trace_events) {
        print_trace_events(analyzer.omega_traces(), analyzer.reversal_traces());
    }
    
    if (!export_trajectory.empty()) {
        analyzer.export_trajectory_csv(export_trajectory);
        std::cout << "\n轨迹已导出: " << export_trajectory << "\n";
    }
    
    if (!export_events.empty()) {
        analyzer.export_events_csv(export_events);
        std::cout << "事件已导出: " << export_events << "\n";
    }
    
    return 0;
}
