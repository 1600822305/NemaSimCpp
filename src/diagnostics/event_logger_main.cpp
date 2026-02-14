#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <map>
#include <sstream>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#ifdef _WIN32
#include <windows.h>
#undef ERROR
#endif

using namespace celegans;

// ================================================================
// Event: 仿真中发生的一个离散事件
// ================================================================
struct Event {
    double time_ms;
    std::string category;   // BEHAVIOR, STATE, ENVIRONMENT, NEUROMOD, MOTOR
    std::string type;        // 具体事件类型
    std::string detail;      // 附加信息
};

// ================================================================
// EventLogger: 边沿检测所有状态变化，记录为事件序列
// ================================================================
class EventLogger {
public:
    EventLogger(double duration_s, unsigned int seed)
        : duration_s_(duration_s), seed_(seed) {}

    void run() {
        std::cout << "  Running simulation... " << std::flush;

        SimulationEngine sim;
        sim.initialize_default();
        sim.set_rng_seed(seed_);

        // 默认环境
        sim.environment().chemical_field().clear();
        Vector2d food{35.0, 25.0};
        sim.environment().chemical_field().add_point_source(food, 1.0);
        sim.environment().soluble_field().add_point_source(food, 0.4);
        sim.reset_transducers();

        double duration_ms = duration_s_ * 1000.0;
        int total_steps = (int)(duration_ms / sim.dt());
        int sample_interval = (int)(50.0 / sim.dt());  // 50ms

        // Find RIVL/RIVR neuron indices for omega trace
        int rivl_idx = -1, rivr_idx = -1;
        for (int i = 0; i < (int)sim.neurons().size(); ++i) {
            if (sim.neurons()[i]->name() == "RIVL") rivl_idx = i;
            if (sim.neurons()[i]->name() == "RIVR") rivr_idx = i;
        }

        // 上一帧状态（边沿检测用）
        bool prev_reversing = false;
        bool prev_omega = false;
        bool prev_sleeping = false;
        bool prev_dmp = false;
        bool prev_tap = false;
        int prev_pumps = 0;
        int prev_eggs = 0;
        int prev_dmp_count = 0;
        double prev_satiety = 0;
        double prev_sickness = 0;
        double prev_food_mem = 0;
        double omega_start_heading = 0;
        double reversal_start_heading = 0;

        // 5-HT 状态追踪
        bool prev_high_5ht = false;

        for (int s = 0; s < total_steps; ++s) {
            sim.step();
            double t = sim.current_time();

            if ((s + 1) % sample_interval != 0) continue;

            // === BEHAVIOR 事件 (structured payload) ===
            bool curr_rev = sim.is_reversing();
            bool curr_omega = sim.is_omega_turning();
            double heading_deg = sim.body().get_head_angle() * 180.0 / 3.14159265358979323846;
            double speed = sim.body().get_speed();
            double curv = sim.body().segments()[0].curvature;
            double dir = sim.body().get_direction();
            double food_dx = food.x - sim.body().get_head_position().x;
            double food_dy = food.y - sim.body().get_head_position().y;
            double food_dist = std::sqrt(food_dx*food_dx + food_dy*food_dy);

            if (curr_rev && !prev_reversing) {
                reversal_start_heading = heading_deg;
                log(t, "BEHAVIOR", "REVERSAL_START",
                    "heading=" + fmt(heading_deg, 1) + " speed=" + fmt(speed, 3)
                    + " food_dist=" + fmt(food_dist, 1));
            }
            if (!curr_rev && prev_reversing) {
                double dur = sim.reversal_duration();
                double turn = heading_deg - reversal_start_heading;
                while (turn > 180.0) turn -= 360.0;
                while (turn < -180.0) turn += 360.0;
                log(t, "BEHAVIOR", "REVERSAL_END",
                    "dur=" + fmt(dur, 0) + "ms turn=" + fmt(turn, 1)
                    + " heading=" + fmt(heading_deg, 1)
                    + " food_dist=" + fmt(food_dist, 1));
            }
            if (curr_omega && !prev_omega) {
                omega_start_heading = heading_deg;
                std::string riv_info;
                if (rivl_idx >= 0 && rivr_idx >= 0) {
                    double rl = sim.neurons()[rivl_idx]->get_transmitter_release_rate();
                    double rr = sim.neurons()[rivr_idx]->get_transmitter_release_rate();
                    riv_info = " RIVL=" + fmt(rl, 3) + " RIVR=" + fmt(rr, 3);
                }
                log(t, "BEHAVIOR", "OMEGA_START",
                    "heading=" + fmt(heading_deg, 1) + " dir=" + fmt(dir, 0)
                    + " curv=" + fmt(curv, 2) + riv_info);
            }
            if (!curr_omega && prev_omega) {
                double turn = heading_deg - omega_start_heading;
                while (turn > 180.0) turn -= 360.0;
                while (turn < -180.0) turn += 360.0;
                log(t, "BEHAVIOR", "OMEGA_END",
                    "heading=" + fmt(heading_deg, 1) + " turn=" + fmt(turn, 1)
                    + " dir=" + fmt(dir, 0) + " curv=" + fmt(curv, 2)
                    + " speed=" + fmt(speed, 3));
            }

            prev_reversing = curr_rev;
            prev_omega = curr_omega;

            // === STATE 事件 ===
            bool curr_sleep = sim.is_sleeping();
            if (curr_sleep && !prev_sleeping) {
                log(t, "STATE", "SLEEP_ONSET", "fatigue=" + fmt(sim.fatigue(), 2));
            }
            if (!curr_sleep && prev_sleeping) {
                log(t, "STATE", "SLEEP_OFFSET", "fatigue=" + fmt(sim.fatigue(), 2));
            }
            prev_sleeping = curr_sleep;

            // Satiety 阈值交叉
            double sat = sim.satiety();
            if (sat > 0.5 && prev_satiety <= 0.5) {
                log(t, "STATE", "SATIETY_HIGH", "satiety=" + fmt(sat, 2));
            }
            if (sat < 0.2 && prev_satiety >= 0.2) {
                log(t, "STATE", "SATIETY_LOW", "satiety=" + fmt(sat, 2));
            }
            prev_satiety = sat;

            // Sickness
            double sick = sim.sickness();
            if (sick > 0.3 && prev_sickness <= 0.3) {
                log(t, "STATE", "SICKNESS_ONSET", "sickness=" + fmt(sick, 2));
            }
            if (sick < 0.1 && prev_sickness >= 0.1) {
                log(t, "STATE", "SICKNESS_CLEAR", "sickness=" + fmt(sick, 2));
            }
            prev_sickness = sick;

            // Food memory (ARS)
            double fmem = sim.food_memory();
            if (fmem > 0.5 && prev_food_mem <= 0.5) {
                log(t, "STATE", "ARS_ACTIVE", "food_mem=" + fmt(fmem, 2));
            }
            if (fmem < 0.1 && prev_food_mem >= 0.1) {
                log(t, "STATE", "ARS_INACTIVE", "food_mem=" + fmt(fmem, 2));
            }
            prev_food_mem = fmem;

            // === MOTOR 事件 ===
            // DMP (排便)
            bool curr_dmp = sim.dmp_active();
            if (curr_dmp && !prev_dmp) {
                log(t, "MOTOR", "DMP_START", "cycle=" + std::to_string(sim.dmp_count() + 1));
            }
            if (!curr_dmp && prev_dmp) {
                log(t, "MOTOR", "DMP_END", "");
            }
            prev_dmp = curr_dmp;

            // Pump events (每10次记录一次)
            int curr_pumps = sim.total_pumps();
            if (curr_pumps > 0 && curr_pumps / 10 > prev_pumps / 10) {
                log(t, "MOTOR", "PUMP_MILESTONE", "total=" + std::to_string(curr_pumps)
                    + " rate=" + fmt(sim.pump_rate_hz(), 1) + "Hz");
            }
            prev_pumps = curr_pumps;

            // Egg laying
            int curr_eggs = (int)sim.egg_laid_count();
            if (curr_eggs > prev_eggs) {
                log(t, "MOTOR", "EGG_LAID", "total=" + std::to_string(curr_eggs));
            }
            prev_eggs = curr_eggs;

            // Tap habituation
            bool curr_tap = sim.tap_active();
            if (curr_tap && !prev_tap) {
                log(t, "ENVIRONMENT", "TAP", "count=" + std::to_string(sim.tap_count()));
            }
            prev_tap = curr_tap;

            // === NEUROMOD 事件 ===
            double sht = sim.neuromodulation().get_concentration("5-HT");
            bool curr_high = sht > 0.35;
            if (curr_high && !prev_high_5ht) {
                log(t, "NEUROMOD", "5HT_HIGH", "conc=" + fmt(sht, 3));
            }
            if (!curr_high && prev_high_5ht) {
                log(t, "NEUROMOD", "5HT_LOW", "conc=" + fmt(sht, 3));
            }
            prev_high_5ht = curr_high;
        }

        std::cout << "Done!\n\n";
    }

    const std::vector<Event>& events() const { return events_; }

    void print_timeline() const {
        std::cout << "========================================\n";
        std::cout << "  EVENT TIMELINE (" << events_.size() << " events)\n";
        std::cout << "========================================\n\n";

        std::cout << std::left
                  << std::setw(10) << "Time(s)"
                  << std::setw(14) << "Category"
                  << std::setw(20) << "Event"
                  << "Detail\n";
        std::cout << std::string(60, '-') << "\n";

        for (const auto& e : events_) {
            std::cout << std::fixed << std::setprecision(2)
                      << std::setw(10) << std::right << (e.time_ms / 1000.0)
                      << "  " << std::left
                      << std::setw(14) << e.category
                      << std::setw(20) << e.type
                      << e.detail << "\n";
        }
    }

    void print_summary() const {
        std::cout << "\n========================================\n";
        std::cout << "  EVENT SUMMARY\n";
        std::cout << "========================================\n\n";

        // 按类别统计
        std::map<std::string, int> cat_counts;
        std::map<std::string, int> type_counts;
        for (const auto& e : events_) {
            cat_counts[e.category]++;
            type_counts[e.type]++;
        }

        for (auto it = cat_counts.begin(); it != cat_counts.end(); ++it) {
            std::cout << "  " << std::setw(14) << std::left << it->first << it->second << " events\n";
        }
        std::cout << "\n";

        for (auto it = type_counts.begin(); it != type_counts.end(); ++it) {
            std::cout << "    " << std::setw(20) << std::left << it->first << it->second << "\n";
        }
    }

    void export_csv(const std::string& filename) const {
        std::ofstream ofs(filename);
        if (!ofs) return;

        ofs << "time_ms,time_s,category,event_type,detail\n";
        for (const auto& e : events_) {
            ofs << std::fixed << std::setprecision(1) << e.time_ms << ","
                << std::setprecision(3) << (e.time_ms / 1000.0) << ","
                << e.category << "," << e.type << ","
                << "\"" << e.detail << "\"\n";
        }
    }

private:
    double duration_s_;
    unsigned int seed_;
    std::vector<Event> events_;

    void log(double t, const std::string& cat, const std::string& type, const std::string& detail) {
        events_.push_back({t, cat, type, detail});
    }

    static std::string fmt(double v, int prec) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(prec) << v;
        return oss.str();
    }
};

int main(int argc, char* argv[]) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    Logger::instance().set_level(LogLevel::ERROR);

    double duration = 120.0;
    unsigned int seed = 123;
    bool show_timeline = true;
    bool show_summary = true;
    std::string export_file;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i+1 < argc) {
            duration = std::atof(argv[++i]);
        } else if (arg == "--seed" && i+1 < argc) {
            seed = std::atoi(argv[++i]);
        } else if (arg == "--no-timeline") {
            show_timeline = false;
        } else if (arg == "--no-summary") {
            show_summary = false;
        } else if (arg == "--export" && i+1 < argc) {
            export_file = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: event_logger [options]\n\n"
                      << "Record all discrete events during simulation\n\n"
                      << "Options:\n"
                      << "  --duration <sec>     Simulation duration (default: 120)\n"
                      << "  --seed <n>           RNG seed (default: 123)\n"
                      << "  --no-timeline        Hide event timeline\n"
                      << "  --no-summary         Hide event summary\n"
                      << "  --export <csv>       Export events to CSV\n"
                      << "  --help / -h          Show this help\n\n"
                      << "Event Categories:\n"
                      << "  BEHAVIOR     Reversal, Omega turn\n"
                      << "  STATE        Sleep, Satiety, Sickness, ARS\n"
                      << "  MOTOR        DMP, Pharyngeal pump, Egg laying\n"
                      << "  ENVIRONMENT  Touch, Tap\n"
                      << "  NEUROMOD     5-HT level changes\n";
            return 0;
        }
    }

    std::cout << "========================================\n";
    std::cout << "  Event Logger\n";
    std::cout << "========================================\n\n";
    std::cout << "  Duration: " << duration << " s\n";
    std::cout << "  Seed:     " << seed << "\n\n";

    EventLogger logger(duration, seed);
    logger.run();

    if (show_timeline) logger.print_timeline();
    if (show_summary) logger.print_summary();

    if (!export_file.empty()) {
        logger.export_csv(export_file);
        std::cout << "\nExported: " << export_file << "\n";
    }

    return 0;
}
