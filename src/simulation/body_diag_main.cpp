// body_diag_main.cpp — Body physics diagnostic tool
// Monitors rod/segment numerical values to diagnose twitching, instability, etc.
// Usage: celegans_body_diag.exe [--duration 5000] [--interval 100] [--seg 5,10,20]
//
// Output: time-series CSV of key body metrics per segment

#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <fstream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <numeric>

using namespace celegans;

struct BodySnapshot {
    double time_ms;
    double speed;
    double head_angle;

    // Per monitored segment
    struct SegData {
        int seg_id;
        double phi_left;        // rod[s].phi
        double phi_right;       // rod[s+1].phi
        double dphi;            // phi[s] - phi[s+1] (curvature proxy)
        double curvature_mm;    // dphi / seg_len [/mm]
        double cx_left, cy_left;
        double cx_right, cy_right;
        double dorsal_act;
        double ventral_act;
        double dv_diff;         // dorsal - ventral
        double dv_raw;          // dorsal_input - ventral_input (raw)
        double dv_drive;        // low-pass filtered drive
        double seg_torque;
    };
    std::vector<SegData> segs;
};

// Compute statistics
struct Stats {
    double mean, stdev, min_val, max_val;
    int sign_changes;
};

Stats compute_stats(const std::vector<double>& v) {
    Stats s{};
    if (v.empty()) return s;
    s.min_val = *std::min_element(v.begin(), v.end());
    s.max_val = *std::max_element(v.begin(), v.end());
    double sum = std::accumulate(v.begin(), v.end(), 0.0);
    s.mean = sum / v.size();
    double sq_sum = 0;
    for (auto x : v) sq_sum += (x - s.mean) * (x - s.mean);
    s.stdev = std::sqrt(sq_sum / v.size());
    s.sign_changes = 0;
    for (size_t i = 1; i < v.size(); ++i) {
        if ((v[i] > 0 && v[i-1] < 0) || (v[i] < 0 && v[i-1] > 0))
            s.sign_changes++;
    }
    return s;
}

int main(int argc, char* argv[]) {
    Logger::instance().set_level(LogLevel::WARN);

    // Defaults
    int duration_ms = 5000;
    int sample_interval_ms = 10;  // sample every 10ms
    std::vector<int> monitor_segs = {0, 5, 10, 15, 20, 25, 30, 35, 40, 47};
    std::string csv_file;
    bool dump_all = false;

    // Parse args
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i + 1 < argc) duration_ms = std::atoi(argv[++i]);
        else if (arg == "--interval" && i + 1 < argc) sample_interval_ms = std::atoi(argv[++i]);
        else if (arg == "--csv" && i + 1 < argc) csv_file = argv[++i];
        else if (arg == "--all") dump_all = true;
        else if (arg == "--seg" && i + 1 < argc) {
            monitor_segs.clear();
            std::string s = argv[++i];
            std::istringstream iss(s);
            std::string tok;
            while (std::getline(iss, tok, ','))
                monitor_segs.push_back(std::atoi(tok.c_str()));
        }
    }

    if (dump_all) {
        monitor_segs.clear();
        for (int s = 0; s < 48; ++s) monitor_segs.push_back(s);
    }

    std::cout << "========================================" << std::endl;
    std::cout << "  BODY PHYSICS DIAGNOSTICS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Duration: " << duration_ms << " ms" << std::endl;
    std::cout << "  Sample interval: " << sample_interval_ms << " ms" << std::endl;
    std::cout << "  Monitored segments: ";
    for (int s : monitor_segs) std::cout << s << " ";
    std::cout << std::endl << std::endl;

    // Initialize simulation
    SimulationEngine sim;
    sim.initialize_default();
    Vector2d food{35.0, 25.0};
    sim.environment().chemical_field().clear();
    sim.environment().chemical_field().add_point_source(food, 1.0);
    sim.environment().soluble_field().add_point_source(food, 0.4);

    // Warmup: 2 seconds
    std::cout << "  Warming up (2s)..." << std::flush;
    int warmup_steps = (int)(2000.0 / sim.dt());
    for (int s = 0; s < warmup_steps; ++s) sim.step();
    std::cout << " done." << std::endl;

    // Collect snapshots
    std::vector<BodySnapshot> snapshots;
    int steps_per_sample = (int)(sample_interval_ms / sim.dt());
    if (steps_per_sample < 1) steps_per_sample = 1;
    int total_samples = duration_ms / sample_interval_ms;

    std::cout << "  Collecting " << total_samples << " samples..." << std::flush;

    const auto& body = sim.body();
    double seg_len_m = body.rods()[1].cx - body.rods()[0].cx;  // approximate
    // Recompute from actual positions
    {
        auto r0 = body.rods()[0];
        auto r1 = body.rods()[1];
        seg_len_m = std::sqrt((r1.cx - r0.cx) * (r1.cx - r0.cx) +
                              (r1.cy - r0.cy) * (r1.cy - r0.cy));
    }
    // Convert to mm for curvature display
    double seg_len_mm = seg_len_m * 1000.0;

    for (int sample = 0; sample < total_samples; ++sample) {
        for (int s = 0; s < steps_per_sample; ++s) sim.step();

        BodySnapshot snap;
        snap.time_ms = 2000.0 + (sample + 1) * sample_interval_ms;
        snap.speed = body.get_speed();
        snap.head_angle = body.get_head_angle() * 180.0 / 3.14159265;

        const auto& rods = body.rods();
        const auto& muscles = body.muscles();

        for (int seg : monitor_segs) {
            if (seg < 0 || seg >= 48) continue;
            BodySnapshot::SegData sd;
            sd.seg_id = seg;
            sd.phi_left = rods[seg].phi;
            sd.phi_right = rods[seg + 1].phi;
            sd.dphi = sd.phi_left - sd.phi_right;
            // Wrap dphi
            while (sd.dphi > 3.14159265) sd.dphi -= 2.0 * 3.14159265;
            while (sd.dphi < -3.14159265) sd.dphi += 2.0 * 3.14159265;
            sd.curvature_mm = (seg_len_mm > 1e-10) ? sd.dphi / seg_len_mm : 0.0;
            sd.cx_left = rods[seg].cx;
            sd.cy_left = rods[seg].cy;
            sd.cx_right = rods[seg + 1].cx;
            sd.cy_right = rods[seg + 1].cy;
            sd.dorsal_act = muscles[seg].dorsal_activation;
            sd.ventral_act = muscles[seg].ventral_activation;
            sd.dv_diff = sd.dorsal_act - sd.ventral_act;
            sd.dv_raw = muscles[seg].dorsal_input - muscles[seg].ventral_input;
            sd.dv_drive = muscles[seg].dv_drive;
            sd.seg_torque = 0;  // not directly accessible, but dv_diff is proxy
            snap.segs.push_back(sd);
        }
        snapshots.push_back(snap);
    }
    std::cout << " done." << std::endl << std::endl;

    // ---- Console summary ----
    std::cout << "========================================" << std::endl;
    std::cout << "  SEGMENT STATISTICS (over " << duration_ms << " ms)" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << std::endl;

    // Header
    std::cout << std::setw(4) << "Seg"
              << std::setw(10) << "Curv avg"
              << std::setw(10) << "Curv std"
              << std::setw(10) << "Curv min"
              << std::setw(10) << "Curv max"
              << std::setw(8) << "SignChg"
              << std::setw(10) << "D-V avg"
              << std::setw(10) << "D-V std"
              << std::setw(10) << "|D-V|max"
              << std::setw(10) << "Raw avg"
              << std::setw(10) << "Raw std"
              << std::setw(10) << "Drive avg"
              << std::setw(10) << "Drive std"
              << std::endl;
    std::cout << std::string(122, '-') << std::endl;

    for (size_t si = 0; si < monitor_segs.size(); ++si) {
        std::vector<double> curv_series, dv_series;
        for (auto& snap : snapshots) {
            if (si < snap.segs.size()) {
                curv_series.push_back(snap.segs[si].curvature_mm);
                dv_series.push_back(snap.segs[si].dv_diff);
            }
        }
        auto cs = compute_stats(curv_series);
        auto ds = compute_stats(dv_series);
        double dv_abs_max = std::max(std::abs(ds.min_val), std::abs(ds.max_val));

        // Raw and drive stats
        std::vector<double> raw_series, drive_series;
        for (auto& snap2 : snapshots) {
            if (si < snap2.segs.size()) {
                raw_series.push_back(snap2.segs[si].dv_raw);
                drive_series.push_back(snap2.segs[si].dv_drive);
            }
        }
        auto rs = compute_stats(raw_series);
        auto drs = compute_stats(drive_series);

        double sign_change_hz = cs.sign_changes / (duration_ms * 0.001);

        std::cout << std::setw(4) << monitor_segs[si]
                  << std::setw(10) << std::fixed << std::setprecision(2) << cs.mean
                  << std::setw(10) << cs.stdev
                  << std::setw(10) << cs.min_val
                  << std::setw(10) << cs.max_val
                  << std::setw(8) << cs.sign_changes
                  << std::setw(10) << std::setprecision(3) << ds.mean
                  << std::setw(10) << ds.stdev
                  << std::setw(10) << std::setprecision(3) << dv_abs_max
                  << std::setw(10) << rs.mean
                  << std::setw(10) << rs.stdev
                  << std::setw(10) << drs.mean
                  << std::setw(10) << drs.stdev
                  << std::endl;
    }

    // Global stats
    std::vector<double> speed_series, heading_series;
    for (auto& snap : snapshots) {
        speed_series.push_back(snap.speed);
        heading_series.push_back(snap.head_angle);
    }
    auto sp = compute_stats(speed_series);

    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  GLOBAL METRICS" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  Speed: mean=" << std::setprecision(2) << sp.mean
              << " std=" << sp.stdev << " mm/s" << std::endl;
    std::cout << "  Head angle range: " << std::setprecision(1)
              << heading_series.front() << " -> " << heading_series.back()
              << " deg" << std::endl;

    // Twitching detection
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  TWITCHING ANALYSIS" << std::endl;
    std::cout << "========================================" << std::endl;

    bool twitching_detected = false;
    for (size_t si = 0; si < monitor_segs.size(); ++si) {
        std::vector<double> curv_series;
        for (auto& snap : snapshots)
            if (si < snap.segs.size())
                curv_series.push_back(snap.segs[si].curvature_mm);

        auto cs = compute_stats(curv_series);
        double sign_hz = cs.sign_changes / (duration_ms * 0.001);

        // Twitching = high frequency sign changes (>3 Hz) with moderate amplitude
        if (sign_hz > 3.0 && cs.stdev > 0.3) {
            std::cout << "  [!!] Seg " << monitor_segs[si]
                      << ": sign_change=" << std::setprecision(1) << sign_hz << " Hz"
                      << "  curv_std=" << std::setprecision(2) << cs.stdev << " /mm"
                      << "  — TWITCHING" << std::endl;
            twitching_detected = true;
        }
    }

    if (!twitching_detected) {
        std::cout << "  No twitching detected (all segments < 3 Hz sign changes)." << std::endl;
    }

    // Phi discontinuity detection
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  PHI DISCONTINUITY CHECK" << std::endl;
    std::cout << "========================================" << std::endl;

    bool phi_issue = false;
    // Check last snapshot for phi jumps between adjacent rods
    if (!snapshots.empty()) {
        const auto& last = snapshots.back();
        const auto& rods = sim.body().rods();
        for (int i = 0; i < 48; ++i) {
            double dphi = rods[i].phi - rods[i + 1].phi;
            while (dphi > 3.14159265) dphi -= 2.0 * 3.14159265;
            while (dphi < -3.14159265) dphi += 2.0 * 3.14159265;
            if (std::abs(dphi) > 0.1) {  // > 5.7 degrees between adjacent rods
                std::cout << "  [!!] Rod " << i << "-" << i + 1
                          << ": dphi=" << std::setprecision(4) << dphi
                          << " rad (" << std::setprecision(1) << dphi * 180 / 3.14159265 << " deg)"
                          << std::endl;
                phi_issue = true;
            }
        }
    }
    if (!phi_issue) {
        std::cout << "  All rod-rod dphi within normal range (< 0.1 rad)." << std::endl;
    }

    // NaN/Inf check
    std::cout << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "  NaN/Inf CHECK" << std::endl;
    std::cout << "========================================" << std::endl;
    {
        const auto& rods = sim.body().rods();
        bool clean = true;
        for (int i = 0; i < 49; ++i) {
            if (std::isnan(rods[i].cx) || std::isinf(rods[i].cx) ||
                std::isnan(rods[i].cy) || std::isinf(rods[i].cy) ||
                std::isnan(rods[i].phi) || std::isinf(rods[i].phi)) {
                std::cout << "  [!!] Rod " << i << ": NaN/Inf detected!" << std::endl;
                clean = false;
            }
        }
        if (clean) std::cout << "  All rods clean (no NaN/Inf)." << std::endl;
    }

    // ---- CSV output ----
    if (!csv_file.empty()) {
        std::ofstream ofs(csv_file);
        if (ofs.is_open()) {
            // Header
            ofs << "time_ms,speed,head_angle";
            for (int seg : monitor_segs)
                ofs << ",seg" << seg << "_curv,seg" << seg << "_dv_diff"
                    << ",seg" << seg << "_d_act,seg" << seg << "_v_act"
                    << ",seg" << seg << "_phi_l,seg" << seg << "_phi_r";
            ofs << "\n";

            for (auto& snap : snapshots) {
                ofs << std::setprecision(1) << snap.time_ms
                    << "," << std::setprecision(4) << snap.speed
                    << "," << std::setprecision(2) << snap.head_angle;
                for (size_t si = 0; si < snap.segs.size(); ++si) {
                    auto& sd = snap.segs[si];
                    ofs << "," << std::setprecision(4) << sd.curvature_mm
                        << "," << std::setprecision(4) << sd.dv_diff
                        << "," << std::setprecision(4) << sd.dorsal_act
                        << "," << std::setprecision(4) << sd.ventral_act
                        << "," << std::setprecision(6) << sd.phi_left
                        << "," << std::setprecision(6) << sd.phi_right;
                }
                ofs << "\n";
            }
            std::cout << std::endl << "  CSV saved: " << csv_file << std::endl;
        }
    }

    std::cout << std::endl;
    return twitching_detected ? 1 : 0;
}
