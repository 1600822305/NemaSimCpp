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
    
    // 神经调质状态 (正交于运动状态)
    double roaming_time_pct = 0;   // 5-HT < 0.35
    double dwelling_time_pct = 0;  // 5-HT >= 0.35
    
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
    BehaviorAnalyzer(double duration_s, unsigned int seed, Vector2d target)
        : duration_s_(duration_s), seed_(seed), target_(target) {}
    
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
        
        // 互斥状态机
        MotionState curr_state = MotionState::FORWARD;
        MotionState prev_state = MotionState::FORWARD;
        double state_start_time = 0;
        double state_start_heading = 0;
        
        // 互斥状态时间累计
        double forward_time = 0, reverse_time = 0, omega_time = 0, pause_time = 0;
        double roaming_time = 0, dwelling_time = 0;
        double near_target_time = 0;
        
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
                
                // 神经调质状态 (正交于运动状态)
                double serotonin = sim.neuromodulation().get_concentration("5-HT");
                if (serotonin < 0.35) {
                    roaming_time += dt_sample;
                } else {
                    dwelling_time += dt_sample;
                }
                
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
                       roaming_time, dwelling_time, near_target_time);
    }
    
    const BehaviorMetrics& metrics() const { return metrics_; }
    const std::vector<BehaviorEvent>& events() const { return events_; }
    const std::vector<TrajectoryPoint>& trajectory() const { return trajectory_; }
    
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
    
    std::vector<TrajectoryPoint> trajectory_;
    std::vector<BehaviorEvent> events_;
    std::vector<BoutInfo> forward_bouts_;
    std::vector<BoutInfo> reverse_bouts_;
    std::vector<BoutInfo> omega_bouts_;
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
                        double roaming_time, double dwelling_time, double near_target_time) {
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
        
        // === 神经调质状态 (正交, 总和 = 100%) ===
        double neuromod_total = roaming_time + dwelling_time;
        if (neuromod_total > 0) {
            metrics_.roaming_time_pct = 100.0 * roaming_time / neuromod_total;
            metrics_.dwelling_time_pct = 100.0 * dwelling_time / neuromod_total;
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
    
    std::cout << "--- 神经调质状态 (正交, 总和=100%) ---\n";
    std::cout << "  Roaming (低5-HT):   " << m.roaming_time_pct << "%\n";
    std::cout << "  Dwelling (高5-HT):  " << m.dwelling_time_pct << "%\n\n";
    
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
                      << "  --export-trajectory <csv>  导出轨迹数据\n"
                      << "  --export-events <csv>      导出行为事件\n"
                      << "  --help / -h                显示帮助\n\n"
                      << "输出指标:\n"
                      << "  - 运动学: 速度、路径效率、净位移\n"
                      << "  - 行为事件: 反转、Omega转弯频率和时长\n"
                      << "  - 状态占比: Roaming/Dwelling/Reversing/Omega\n"
                      << "  - 趋化性: CI、目标距离、方向持续性\n"
                      << "  - 头部运动: 摆动幅度和频率\n";
            return 0;
        }
    }
    
    std::cout << "========================================\n";
    std::cout << "  行为分析器 (科研级)\n";
    std::cout << "========================================\n\n";
    std::cout << "  仿真时长:  " << duration << " s\n";
    std::cout << "  随机种子:  " << seed << "\n";
    std::cout << "  目标位置:  (" << target.x << ", " << target.y << ")\n\n";
    
    BehaviorAnalyzer analyzer(duration, seed, target);
    analyzer.run();
    
    print_metrics(analyzer.metrics(), verbose);
    
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
