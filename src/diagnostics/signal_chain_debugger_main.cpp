// signal_chain_debugger_main.cpp — 事件级信号链快照转储
// Step 120: 在每个行为事件(omega/reversal start/end)时转储完整信号链状态，
// 方便 AI 辅助分析符号链、增益校准和断裂点定位。
//
// Usage:
//   signal_chain_debugger [--duration 60] [--seed 42] [--target-x 20] [--target-y 0]
//   signal_chain_debugger --events omega         # 仅显示 omega 事件
//   signal_chain_debugger --events reversal      # 仅显示 reversal 事件
//   signal_chain_debugger --events all           # 全部事件 (默认)
//   signal_chain_debugger --continuous 200       # 每 200ms 连续快照
//   signal_chain_debugger --summary              # 仅汇总统计，不打印每个事件
#include "simulation/simulation_engine.h"
#include "neuron/multi_compartment.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <algorithm>
#include <numeric>
#include <sstream>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

using namespace celegans;

// ================================================================
// Helpers
// ================================================================
static double wrap_angle(double a) {
    while (a > M_PI) a -= 2 * M_PI;
    while (a < -M_PI) a += 2 * M_PI;
    return a;
}
static double deg(double rad) { return rad * 180.0 / M_PI; }

// ================================================================
// Signal chain snapshot — all relevant state at one moment
// ================================================================
struct SignalSnapshot {
    double time_s;

    // --- Position / kinematics ---
    double pos_x, pos_y;
    double heading_deg;
    double speed;
    double direction;

    // --- Food geometry ---
    double food_dist;
    double food_angle_deg;       // signed angle: positive = food LEFT

    // --- Sensory ---
    double concentration;
    double dCdt_filtered;
    double awc_pref;
    double awcl_rel, awcr_rel;
    double asel_rel, aser_rel;

    // --- Interneurons ---
    double aial_rel, aiar_rel;
    double aibl_rel, aibr_rel;
    double aiyl_rel, aiyr_rel;

    // --- Command neurons ---
    double aval_rel, avar_rel;
    double avbl_rel, avbr_rel;

    // --- RIA klinotaxis ---
    double ria_ca_diff_ac;       // filtered AC component
    double ria_ca_diff_mean;     // DC baseline

    // --- Gradient signal chain ---
    double grad_x, grad_y;
    double grad_mag;
    double grad_perp;            // perpendicular to heading (+ = food LEFT)

    // --- Reversal / omega state ---
    bool is_reversing;
    bool is_omega;
    double pre_rev_dorsal_tone;
    double riv_post_rev_amp_l, riv_post_rev_amp_r;
    double riv_omega_peak_l, riv_omega_peak_r;

    // --- RIV neurons ---
    double rivl_rel, rivr_rel;

    // --- Muscle state (head 6 segments) ---
    double head_force_diff;      // mean D-V force differential
    double head_curv;            // mean |curvature|

    // --- Neuromodulators ---
    double serotonin, dopamine, octopamine, tyramine;

    // --- Internal states ---
    double satiety, sickness, food_memory, fatigue;
};

// ================================================================
// Event record
// ================================================================
enum class EventType {
    REVERSAL_START,
    REVERSAL_END,
    OMEGA_START,
    OMEGA_END,
    CONTINUOUS       // periodic snapshot
};

struct EventRecord {
    EventType type;
    SignalSnapshot snap;
    // Post-event data (filled at event end)
    double duration_ms = 0;
    double heading_change_deg = 0;
    double food_angle_before_deg = 0;
    double food_angle_after_deg = 0;
    bool toward_food = false;    // omega only: did the turn reduce |food_angle|?
};

static const char* event_name(EventType t) {
    switch (t) {
        case EventType::REVERSAL_START: return "REV_START";
        case EventType::REVERSAL_END:   return "REV_END";
        case EventType::OMEGA_START:    return "OMEGA_START";
        case EventType::OMEGA_END:      return "OMEGA_END";
        case EventType::CONTINUOUS:     return "SNAPSHOT";
    }
    return "UNKNOWN";
}

// ================================================================
// Capture snapshot from simulation state
// ================================================================
static SignalSnapshot capture(const SimulationEngine& sim, Vector2d food_pos) {
    SignalSnapshot s{};
    s.time_s = sim.current_time() / 1000.0;

    // Position / kinematics
    auto hp = sim.body().get_head_position();
    s.pos_x = hp.x;
    s.pos_y = hp.y;
    s.heading_deg = deg(sim.body().get_head_angle());
    s.speed = sim.body().get_speed();
    s.direction = sim.body().get_direction();

    // Food geometry
    Vector2d to_food = {food_pos.x - hp.x, food_pos.y - hp.y};
    s.food_dist = to_food.norm();
    double heading_rad = sim.body().get_head_angle();
    s.food_angle_deg = deg(wrap_angle(std::atan2(to_food.y, to_food.x) - heading_rad));

    // Sensory
    s.concentration = sim.environment().sample_chemical(hp);
    s.dCdt_filtered = sim.dCdt_filtered();
    s.awc_pref = sim.awc_pref_cached();

    // Gradient
    auto grad = sim.environment().chemical_field().gradient(hp);
    s.grad_x = grad.x;
    s.grad_y = grad.y;
    s.grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);
    s.grad_perp = -std::sin(heading_rad) * grad.x + std::cos(heading_rad) * grad.y;

    // Neuron release rates
    const auto& neurons = sim.neurons();
    int nn = static_cast<int>(neurons.size());
    auto fid = [&](const char* name) -> int {
        for (int i = 0; i < nn; ++i) if (neurons[i]->info().name == name) return i;
        return -1;
    };
    auto rel = [&](int id) -> double {
        return (id >= 0 && id < nn) ? neurons[id]->get_transmitter_release_rate() : 0.0;
    };

    int awcl = fid("AWCL"), awcr = fid("AWCR");
    int asel = fid("ASEL"), aser = fid("ASER");
    int aial = fid("AIAL"), aiar = fid("AIAR");
    int aibl = fid("AIBL"), aibr = fid("AIBR");
    int aiyl = fid("AIYL"), aiyr = fid("AIYR");
    int aval = fid("AVAL"), avar = fid("AVAR");
    int avbl = fid("AVBL"), avbr = fid("AVBR");
    int rivl = fid("RIVL"), rivr = fid("RIVR");

    s.awcl_rel = rel(awcl);  s.awcr_rel = rel(awcr);
    s.asel_rel = rel(asel);  s.aser_rel = rel(aser);
    s.aial_rel = rel(aial);  s.aiar_rel = rel(aiar);
    s.aibl_rel = rel(aibl);  s.aibr_rel = rel(aibr);
    s.aiyl_rel = rel(aiyl);  s.aiyr_rel = rel(aiyr);
    s.aval_rel = rel(aval);  s.avar_rel = rel(avar);
    s.avbl_rel = rel(avbl);  s.avbr_rel = rel(avbr);
    s.rivl_rel = rel(rivl);  s.rivr_rel = rel(rivr);

    // RIA klinotaxis
    s.ria_ca_diff_ac = sim.ria_ca_diff_filtered();
    s.ria_ca_diff_mean = sim.ria_ca_diff_mean();

    // Reversal / omega state
    s.is_reversing = sim.is_reversing();
    s.is_omega = sim.is_omega_turning();
    s.pre_rev_dorsal_tone = sim.pre_rev_dorsal_tone();
    s.riv_post_rev_amp_l = sim.riv_post_rev_amp_l();
    s.riv_post_rev_amp_r = sim.riv_post_rev_amp_r();
    s.riv_omega_peak_l = sim.riv_omega_peak_l();
    s.riv_omega_peak_r = sim.riv_omega_peak_r();

    // Muscle state (head 6 segments)
    double fd_sum = 0, curv_sum = 0;
    for (int i = 0; i < 6; ++i) {
        fd_sum += sim.body().muscles().get_force_differential(i);
        curv_sum += std::abs(sim.body().get_local_curvature(i));
    }
    s.head_force_diff = fd_sum / 6.0;
    s.head_curv = curv_sum / 6.0;

    // Neuromodulators
    s.serotonin = sim.neuromodulation().get_concentration("5-HT");
    s.dopamine = sim.neuromodulation().get_concentration("DA");
    s.octopamine = sim.neuromodulation().get_concentration("OA");
    s.tyramine = sim.neuromodulation().get_concentration("TA");

    // Internal states
    s.satiety = sim.satiety();
    s.sickness = sim.sickness();
    s.food_memory = sim.food_memory();
    s.fatigue = sim.fatigue();

    return s;
}

// ================================================================
// Print single event snapshot
// ================================================================
static void print_event(const EventRecord& ev, int index) {
    const auto& s = ev.snap;
    std::cout << "\n+--------------------------------------------------------------+\n";
    std::cout << "|  " << std::left << std::setw(12) << event_name(ev.type)
              << " #" << std::setw(3) << index
              << "  t=" << std::fixed << std::setprecision(2) << s.time_s << "s"
              << std::string(30, ' ') << "|\n";
    std::cout << "+--------------------------------------------------------------+\n";

    auto row = [](const char* label, auto... args) {
        std::cout << "|  " << std::left << std::setw(22) << label;
        ((std::cout << args), ...);
        std::cout << "\n";
    };

    // Position
    std::cout << "|  --- Position / Kinematics ---\n";
    std::cout << std::fixed << std::setprecision(3);
    row("pos:",        "(",  s.pos_x, ", ", s.pos_y, ") mm");
    row("heading:",    s.heading_deg, "°");
    row("speed:",      s.speed, " mm/s  dir=", s.direction > 0 ? "FWD" : "REV");
    row("food_dist:",  s.food_dist, " mm");
    row("food_angle:", s.food_angle_deg, "°  (",
        s.food_angle_deg > 0 ? "food LEFT" : "food RIGHT", ")");

    // Sensory chain
    std::cout << "|  --- Sensory (gradient -> neurons) ---\n";
    std::cout << std::scientific << std::setprecision(3);
    row("concentration:", s.concentration);
    row("dC/dt_filtered:", s.dCdt_filtered);
    row("gradient:", "(", s.grad_x, ", ", s.grad_y, ")  mag=", s.grad_mag);
    std::cout << std::fixed << std::setprecision(4);
    row("grad_perp:", s.grad_perp, (s.grad_perp > 0 ? "  (food LEFT)" : "  (food RIGHT)"));
    row("awc_pref:", s.awc_pref, (s.awc_pref >= 0 ? "  (naive/attractive)" : "  (aversive)"));
    row("AWC L/R rel:", s.awcl_rel, " / ", s.awcr_rel);
    row("ASE L/R rel:", s.asel_rel, " / ", s.aser_rel);

    // Interneurons
    std::cout << "|  --- Interneurons ---\n";
    row("AIA L/R rel:", s.aial_rel, " / ", s.aiar_rel);
    row("AIB L/R rel:", s.aibl_rel, " / ", s.aibr_rel);
    row("AIY L/R rel:", s.aiyl_rel, " / ", s.aiyr_rel);

    // Command neurons
    std::cout << "|  --- Command Neurons ---\n";
    row("AVA L/R rel:", s.aval_rel, " / ", s.avar_rel,
        "  mean=", (s.aval_rel + s.avar_rel) * 0.5);
    row("AVB L/R rel:", s.avbl_rel, " / ", s.avbr_rel,
        "  mean=", (s.avbl_rel + s.avbr_rel) * 0.5);
    double ava_m = (s.aval_rel + s.avar_rel) * 0.5;
    double avb_m = (s.avbl_rel + s.avbr_rel) * 0.5;
    row("AVA-AVB balance:", ava_m - avb_m,
        (ava_m > avb_m ? "  (AVA dominant → reversal)" : "  (AVB dominant → forward)"));

    // RIA klinotaxis
    std::cout << "|  --- Klinotaxis (RIA->SMB) ---\n";
    row("RIA Ca² AC:", s.ria_ca_diff_ac, "  DC_mean=", s.ria_ca_diff_mean);

    // Reversal / omega chain
    std::cout << "|  --- Reversal / Omega Chain ---\n";
    row("is_reversing:", (s.is_reversing ? "YES" : "NO"),
        "  is_omega: ", (s.is_omega ? "YES" : "NO"));
    row("pre_rev_dors_tone:", s.pre_rev_dorsal_tone);
    row("riv_post_amp L/R:", s.riv_post_rev_amp_l, " / ", s.riv_post_rev_amp_r);
    if (s.riv_post_rev_amp_l + s.riv_post_rev_amp_r > 0.01) {
        double lr = (s.riv_post_rev_amp_l - s.riv_post_rev_amp_r) /
                    (s.riv_post_rev_amp_l + s.riv_post_rev_amp_r);
        row("  amp_lr_ratio:", lr, (lr > 0 ? "  (L dominant → LEFT turn)" : "  (R dominant → RIGHT turn)"));
    }
    row("RIV L/R rel:", s.rivl_rel, " / ", s.rivr_rel);
    row("omega_peak L/R:", s.riv_omega_peak_l, " / ", s.riv_omega_peak_r);
    if (s.riv_omega_peak_l + s.riv_omega_peak_r > 0.001) {
        double plr = (s.riv_omega_peak_l - s.riv_omega_peak_r) /
                     (s.riv_omega_peak_l + s.riv_omega_peak_r);
        row("  peak_lr_ratio:", plr,
            (plr > 0 ? "  (L>R → DORSAL boost → LEFT turn)" :
                       "  (R>L → VENTRAL boost → RIGHT turn)"));
    }

    // Muscles
    std::cout << "|  --- Muscles (head) ---\n";
    row("force_diff:", s.head_force_diff,
        (s.head_force_diff > 0 ? "  (dorsal > ventral)" : "  (ventral > dorsal)"));
    row("mean |curvature|:", s.head_curv, " /mm");

    // Neuromodulators
    std::cout << "|  --- Neuromodulators ---\n";
    row("5-HT:", s.serotonin, "  DA:", s.dopamine,
        "  OA:", s.octopamine, "  TA:", s.tyramine);

    // Internal states
    std::cout << "|  --- Internal States ---\n";
    row("satiety:", s.satiety, "  sickness:", s.sickness,
        "  food_mem:", s.food_memory, "  fatigue:", s.fatigue);

    // Post-event analysis (for END events)
    if (ev.type == EventType::REVERSAL_END || ev.type == EventType::OMEGA_END) {
        std::cout << "|  --- Event Result ---\n";
        std::cout << std::fixed << std::setprecision(1);
        row("duration:", ev.duration_ms, " ms");
        row("heading_change:", ev.heading_change_deg, "°");
        row("food_angle:", ev.food_angle_before_deg, "° → ", ev.food_angle_after_deg, "°");
        if (ev.type == EventType::OMEGA_END) {
            row("toward_food:", (ev.toward_food ? "YES ✓" : "NO ✗"));
        }
    }

    std::cout << "+--------------------------------------------------------------+\n";
}

// ================================================================
// Print summary statistics
// ================================================================
static void print_summary(const std::vector<EventRecord>& events) {
    int rev_count = 0, omega_count = 0;
    int omega_toward = 0;
    double omega_angle_sum = 0;
    std::vector<double> rev_durations, omega_durations;
    std::vector<double> omega_angles;
    // Klinokinesis: compare reversal rates when dC/dt > 0 vs < 0
    int rev_up_gradient = 0, rev_down_gradient = 0;
    int samples_up = 0, samples_down = 0;

    for (const auto& ev : events) {
        if (ev.type == EventType::REVERSAL_START) {
            if (ev.snap.dCdt_filtered > 0) samples_up++;
            else samples_down++;
        }
        if (ev.type == EventType::REVERSAL_END) {
            rev_count++;
            rev_durations.push_back(ev.duration_ms);
        }
        if (ev.type == EventType::OMEGA_END) {
            omega_count++;
            omega_durations.push_back(ev.duration_ms);
            omega_angles.push_back(std::abs(ev.heading_change_deg));
            omega_angle_sum += std::abs(ev.heading_change_deg);
            if (ev.toward_food) omega_toward++;
        }
    }

    std::cout << "\n========================================\n";
    std::cout << "  SIGNAL CHAIN SUMMARY\n";
    std::cout << "========================================\n\n";

    std::cout << std::fixed << std::setprecision(1);
    std::cout << "  Reversals:       " << rev_count << "\n";
    if (!rev_durations.empty()) {
        double mean_d = std::accumulate(rev_durations.begin(), rev_durations.end(), 0.0) / rev_durations.size();
        std::cout << "  Rev mean dur:    " << mean_d << " ms\n";
    }

    std::cout << "  Omega turns:     " << omega_count << "\n";
    if (omega_count > 0) {
        std::cout << "  Omega toward%:   " << (100.0 * omega_toward / omega_count) << "%"
                  << " (" << omega_toward << "/" << omega_count << ")\n";
        std::cout << "  Omega mean |Δθ|: " << (omega_angle_sum / omega_count) << "°\n";
        if (!omega_durations.empty()) {
            double mean_od = std::accumulate(omega_durations.begin(), omega_durations.end(), 0.0) / omega_durations.size();
            std::cout << "  Omega mean dur:  " << mean_od << " ms\n";
        }
    }

    // Omega direction sign chain verification
    std::cout << "\n  --- Omega Direction Sign Chain ---\n";
    int sign_correct = 0, sign_total = 0;
    for (const auto& ev : events) {
        if (ev.type != EventType::OMEGA_START) continue;
        sign_total++;
        // Verify: grad_perp > 0 (food LEFT) → peak_l > peak_r → DORSAL boost → LEFT turn
        bool grad_says_left = ev.snap.grad_perp > 0;
        bool peak_l_dominant = ev.snap.riv_omega_peak_l > ev.snap.riv_omega_peak_r;
        bool signs_match = (grad_says_left == peak_l_dominant);
        if (signs_match) sign_correct++;
    }
    if (sign_total > 0) {
        std::cout << "  grad→peak sign consistent: " << sign_correct << "/" << sign_total
                  << " (" << (100.0 * sign_correct / sign_total) << "%)\n";
    }

    // Klinokinesis verification
    std::cout << "\n  --- Klinokinesis dC/dt ---\n";
    int rev_start_dCdt_neg = 0, rev_start_dCdt_pos = 0, rev_start_total = 0;
    for (const auto& ev : events) {
        if (ev.type != EventType::REVERSAL_START) continue;
        rev_start_total++;
        if (ev.snap.dCdt_filtered < 0) rev_start_dCdt_neg++;
        else rev_start_dCdt_pos++;
    }
    if (rev_start_total > 0) {
        std::cout << "  Reversals when dC/dt<0: " << rev_start_dCdt_neg
                  << "/" << rev_start_total << " ("
                  << (100.0 * rev_start_dCdt_neg / rev_start_total) << "%)\n";
        std::cout << "  Reversals when dC/dt>0: " << rev_start_dCdt_pos
                  << "/" << rev_start_total << " ("
                  << (100.0 * rev_start_dCdt_pos / rev_start_total) << "%)\n";
        if (rev_start_dCdt_neg > rev_start_dCdt_pos) {
            std::cout << "  → Klinokinesis ACTIVE: more reversals down-gradient ✓\n";
        } else {
            std::cout << "  → Klinokinesis WEAK or REVERSED ✗\n";
        }
    }
}

// ================================================================
// Main
// ================================================================
int main(int argc, char* argv[]) {
    Logger::instance().set_level(LogLevel::ERROR);

    double duration_s = 60.0;
    int seed = 42;
    double target_x = 20.0, target_y = 0.0;
    std::string event_filter = "all";   // "all", "omega", "reversal"
    int continuous_ms = 0;              // 0 = off, >0 = periodic snapshot interval
    bool summary_only = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i + 1 < argc) duration_s = std::atof(argv[++i]);
        else if (arg == "--seed" && i + 1 < argc) seed = std::atoi(argv[++i]);
        else if (arg == "--target-x" && i + 1 < argc) target_x = std::atof(argv[++i]);
        else if (arg == "--target-y" && i + 1 < argc) target_y = std::atof(argv[++i]);
        else if (arg == "--events" && i + 1 < argc) event_filter = argv[++i];
        else if (arg == "--continuous" && i + 1 < argc) continuous_ms = std::atoi(argv[++i]);
        else if (arg == "--summary") summary_only = true;
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: signal_chain_debugger [options]\n\n"
                      << "Dumps complete signal chain snapshot at each behavioral event.\n"
                      << "Designed for AI-assisted analysis of chemotaxis signal chains.\n\n"
                      << "Options:\n"
                      << "  --duration <sec>     Simulation duration (default: 60)\n"
                      << "  --seed <n>           Random seed (default: 42)\n"
                      << "  --target-x/y <mm>    Food position (default: 20, 0)\n"
                      << "  --events <filter>    omega|reversal|all (default: all)\n"
                      << "  --continuous <ms>    Periodic snapshot interval (0=off)\n"
                      << "  --summary            Only print summary, no per-event detail\n"
                      << "  --help / -h          Show this help\n\n"
                      << "Output: structured text with labeled fields per event.\n";
            return 0;
        }
    }

    Vector2d food_pos = {target_x, target_y};

    std::cout << "========================================\n";
    std::cout << "  Signal Chain Debugger\n";
    std::cout << "========================================\n\n";
    std::cout << "  Duration:   " << duration_s << " s\n";
    std::cout << "  Seed:       " << seed << "\n";
    std::cout << "  Food:       default (35, 35) unless --target-x/y specified\n";
    std::cout << "  Events:     " << event_filter << "\n";
    if (continuous_ms > 0) {
        std::cout << "  Continuous: " << continuous_ms << " ms\n";
    }
    std::cout << "\n  Running simulation... " << std::flush;

    // --- Initialize simulation ---
    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(seed);

    // Only override environment if user explicitly set food position
    // Default: food at (35,35) from initialize_default()
    bool custom_food = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--target-x" || a == "--target-y") { custom_food = true; break; }
    }
    if (custom_food) {
        sim.environment().chemical_field().clear();
        sim.environment().chemical_field().add_point_source(food_pos, 1.0);
        sim.environment().soluble_field().add_point_source(food_pos, 0.4);
        sim.reset_transducers();
    } else {
        food_pos = {35.0, 35.0};  // match initialize_default()
    }

    double duration_ms = duration_s * 1000.0;
    int total_steps = static_cast<int>(duration_ms / sim.dt());
    int sample_interval = static_cast<int>(50.0 / sim.dt());  // 50ms

    bool show_omega = (event_filter == "all" || event_filter == "omega");
    bool show_reversal = (event_filter == "all" || event_filter == "reversal");

    // --- State tracking ---
    bool prev_rev = false, prev_omega = false;
    SignalSnapshot rev_start_snap{}, omega_start_snap{};
    double rev_start_heading = 0, omega_start_heading = 0;
    double rev_start_food_angle = 0, omega_start_food_angle = 0;
    double warmup_ms = 3000.0;  // skip first 3s to avoid spurious initial events

    std::vector<EventRecord> all_events;
    int event_index = 0;
    int continuous_counter = 0;
    int continuous_interval = (continuous_ms > 0)
        ? static_cast<int>(continuous_ms / sim.dt()) : 0;

    for (int s = 0; s < total_steps; ++s) {
        sim.step();

        bool curr_rev = sim.is_reversing();
        bool curr_omega = sim.is_omega_turning();

        // Edge detection only at sample intervals
        // CRITICAL: do NOT update prev_rev/prev_omega between samples,
        // otherwise transitions between samples are silently swallowed.
        if (s % sample_interval != 0) continue;

        // Continuous snapshots
        if (continuous_interval > 0 && (s / sample_interval) % (continuous_interval / (int)(50.0 / sim.dt()) + 1) == 0 && s > 0) {
            EventRecord ev;
            ev.type = EventType::CONTINUOUS;
            ev.snap = capture(sim, food_pos);
            all_events.push_back(ev);
            if (!summary_only) {
                print_event(ev, continuous_counter++);
            }
        }

        // Skip warmup period (but still update prev state)
        if (sim.current_time() < warmup_ms) {
            prev_rev = curr_rev;
            prev_omega = curr_omega;
            continue;
        }

        auto snap = capture(sim, food_pos);

        // --- Reversal START ---
        if (curr_rev && !prev_rev) {
            rev_start_snap = snap;
            rev_start_heading = snap.heading_deg;
            rev_start_food_angle = snap.food_angle_deg;

            EventRecord ev;
            ev.type = EventType::REVERSAL_START;
            ev.snap = snap;
            all_events.push_back(ev);
            if (!summary_only && show_reversal) {
                print_event(ev, event_index++);
            }
        }

        // --- Reversal END ---
        if (!curr_rev && prev_rev) {
            EventRecord ev;
            ev.type = EventType::REVERSAL_END;
            ev.snap = snap;
            ev.duration_ms = (snap.time_s - rev_start_snap.time_s) * 1000.0;
            ev.heading_change_deg = snap.heading_deg - rev_start_heading;
            ev.food_angle_before_deg = rev_start_food_angle;
            ev.food_angle_after_deg = snap.food_angle_deg;
            all_events.push_back(ev);
            if (!summary_only && show_reversal) {
                print_event(ev, event_index++);
            }
        }

        // --- Omega START ---
        if (curr_omega && !prev_omega) {
            omega_start_snap = snap;
            omega_start_heading = snap.heading_deg;
            omega_start_food_angle = snap.food_angle_deg;

            EventRecord ev;
            ev.type = EventType::OMEGA_START;
            ev.snap = snap;
            all_events.push_back(ev);
            if (!summary_only && show_omega) {
                print_event(ev, event_index++);
            }
        }

        // --- Omega END ---
        if (!curr_omega && prev_omega) {
            EventRecord ev;
            ev.type = EventType::OMEGA_END;
            ev.snap = snap;
            ev.duration_ms = (snap.time_s - omega_start_snap.time_s) * 1000.0;
            ev.heading_change_deg = snap.heading_deg - omega_start_heading;
            ev.food_angle_before_deg = omega_start_food_angle;
            ev.food_angle_after_deg = snap.food_angle_deg;
            ev.toward_food = std::abs(snap.food_angle_deg) < std::abs(omega_start_food_angle);
            all_events.push_back(ev);
            if (!summary_only && show_omega) {
                print_event(ev, event_index++);
            }
        }

        prev_rev = curr_rev;
        prev_omega = curr_omega;
    }

    std::cout << "Done!\n";

    // Summary
    print_summary(all_events);

    return 0;
}
