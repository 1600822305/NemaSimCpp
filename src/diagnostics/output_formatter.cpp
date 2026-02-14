#include "diagnostics/output_formatter.h"
#include <iomanip>

namespace celegans {

void OutputFormatter::print_neuron_stats(std::ostream& out,
                                         const std::vector<DiagnosticTracker::NeuronStats>& stats,
                                         Format format) {
    if (format == Format::JSON) {
        out << "{\n  \"neurons\": [\n";
        for (size_t i = 0; i < stats.size(); ++i) {
            const auto& s = stats[i];
            out << "    {\n"
                << "      \"name\": \"" << s.name << "\",\n"
                << "      \"mean_v\": " << std::fixed << std::setprecision(2) << s.mean_v << ",\n"
                << "      \"v_swing\": " << s.v_swing << ",\n"
                << "      \"mean_release\": " << std::setprecision(3) << s.mean_release << ",\n"
                << "      \"mean_i_syn\": " << std::setprecision(2) << s.mean_i_syn << ",\n"
                << "      \"mean_i_ext\": " << s.mean_i_ext << "\n"
                << "    }" << (i + 1 < stats.size() ? "," : "") << "\n";
        }
        out << "  ]\n}\n";
    }
    else if (format == Format::CSV) {
        out << "name,mean_v,v_min,v_max,v_swing,mean_release,mean_i_syn,mean_i_ext\n";
        for (const auto& s : stats) {
            out << s.name << ","
                << std::fixed << std::setprecision(2)
                << s.mean_v << "," << s.v_min << "," << s.v_max << "," << s.v_swing << ","
                << std::setprecision(3) << s.mean_release << ","
                << std::setprecision(2) << s.mean_i_syn << "," << s.mean_i_ext << "\n";
        }
    }
    else {  // TEXT
        out << "\n========================================\n";
        out << "  NEURON STATISTICS (" << stats.size() << " neurons)\n";
        out << "========================================\n\n";
        out << std::left << std::setw(12) << "Neuron"
            << std::right << std::setw(10) << "Mean V"
            << std::setw(10) << "Swing"
            << std::setw(10) << "Release"
            << std::setw(10) << "I_syn"
            << std::setw(10) << "I_ext" << "\n";
        out << std::string(62, '-') << "\n";
        
        for (const auto& s : stats) {
            out << std::left << std::setw(12) << s.name
                << std::right << std::fixed << std::setprecision(1)
                << std::setw(10) << s.mean_v
                << std::setw(10) << s.v_swing
                << std::setprecision(3)
                << std::setw(10) << s.mean_release
                << std::setprecision(1)
                << std::setw(10) << s.mean_i_syn
                << std::setw(10) << s.mean_i_ext << "\n";
        }
    }
}

void OutputFormatter::print_behavior_metrics(std::ostream& out,
                                             const DiagnosticTracker::BehaviorMetrics& metrics,
                                             Format format) {
    if (format == Format::JSON) {
        out << "{\n"
            << "  \"ci\": " << std::fixed << std::setprecision(3) << metrics.ci << ",\n"
            << "  \"mean_speed\": " << metrics.mean_speed << ",\n"
            << "  \"reversal_rate\": " << metrics.reversal_rate << ",\n"
            << "  \"omega_rate\": " << metrics.omega_rate << ",\n"
            << "  \"omega_per_reversal\": " << metrics.omega_per_reversal << ",\n"
            << "  \"near_target_pct\": " << std::setprecision(1) << metrics.near_target_pct << ",\n"
            << "  \"dv_ratio\": " << std::setprecision(2) << metrics.dv_ratio << "\n"
            << "}\n";
    }
    else if (format == Format::CSV) {
        out << "ci,mean_speed,reversal_rate,omega_rate,omega_per_reversal,near_target_pct,dv_ratio\n"
            << std::fixed << std::setprecision(3)
            << metrics.ci << "," << metrics.mean_speed << ","
            << metrics.reversal_rate << "," << metrics.omega_rate << ","
            << metrics.omega_per_reversal << ","
            << std::setprecision(1) << metrics.near_target_pct << ","
            << std::setprecision(2) << metrics.dv_ratio << "\n";
    }
    else {  // TEXT
        out << "\n========================================\n";
        out << "  BEHAVIOR METRICS\n";
        out << "========================================\n\n";
        out << std::fixed << std::setprecision(3);
        out << "  Chemotaxis Index:    " << metrics.ci << "\n";
        out << "  Mean Speed:          " << metrics.mean_speed << " mm/s\n";
        out << "  Reversal Rate:       " << metrics.reversal_rate << " Hz\n";
        out << "  Omega Rate:          " << metrics.omega_rate << " Hz\n";
        out << "  Omega/Reversal:      " << metrics.omega_per_reversal << "\n";
        out << std::setprecision(1);
        out << "  Near Target:         " << metrics.near_target_pct << "%\n";
        out << std::setprecision(2);
        out << "  D/V Ratio:           " << metrics.dv_ratio << "\n";
    }
}

void OutputFormatter::print_system_summary(std::ostream& out,
                                           const DiagnosticTracker& tracker,
                                           Format format) {
    const auto& sys = tracker.get_system_data();
    
    if (format == Format::TEXT) {
        out << "\n========================================\n";
        out << "  SYSTEM SUMMARY\n";
        out << "========================================\n\n";
        
        if (!sys.time.empty()) {
            double duration_s = (sys.time.back() - sys.time.front()) / 1000.0;
            out << "  Duration:            " << std::fixed << std::setprecision(1) << duration_s << " s\n";
            out << "  Samples:             " << sys.time.size() << "\n";
        }
        
        if (!sys.serotonin.empty()) {
            out << "\n  Neuromodulation (final):\n";
            out << "    5-HT:              " << std::setprecision(3) << sys.serotonin.back() << "\n";
            out << "    DA:                " << sys.dopamine.back() << "\n";
            out << "    OA:                " << sys.octopamine.back() << "\n";
            out << "    TA:                " << sys.tyramine.back() << "\n";
        }
        
        if (!sys.satiety.empty()) {
            out << "\n  Internal State (final):\n";
            out << "    Satiety:           " << std::setprecision(3) << sys.satiety.back() << "\n";
            out << "    Sickness:          " << sys.sickness.back() << "\n";
            out << "    Fatigue:           " << sys.fatigue.back() << "\n";
            out << "    Sleeping:          " << (sys.is_sleeping.back() ? "YES" : "NO") << "\n";
        }
        
        out << "\n  Events:\n";
        out << "    Reversals:         " << sys.reversal_times.size() << "\n";
        out << "    Omega Turns:       " << sys.omega_times.size() << "\n";
    }
}

void OutputFormatter::export_time_series(std::ostream& out,
                                         const DiagnosticTracker& tracker,
                                         const std::vector<std::string>& neuron_names) {
    const auto& sys = tracker.get_system_data();
    
    // CSV 表头
    out << "time_ms,speed,curvature,heading,x,y,5HT,DA,OA,TA,satiety,sickness,fatigue,sleeping";
    for (const auto& name : neuron_names) {
        out << "," << name << "_V," << name << "_S";
    }
    out << "\n";
    
    // 数据行
    for (size_t i = 0; i < sys.time.size(); ++i) {
        out << std::fixed << std::setprecision(2)
            << sys.time[i] << ","
            << sys.speed[i] << ","
            << std::setprecision(4) << sys.curvature[i] << ","
            << std::setprecision(1) << sys.heading[i] << ","
            << std::setprecision(2) << sys.position[i].x << "," << sys.position[i].y << ","
            << std::setprecision(3)
            << sys.serotonin[i] << "," << sys.dopamine[i] << ","
            << sys.octopamine[i] << "," << sys.tyramine[i] << ","
            << sys.satiety[i] << "," << sys.sickness[i] << "," << sys.fatigue[i] << ","
            << sys.is_sleeping[i];
        
        for (const auto& name : neuron_names) {
            const auto* data = tracker.get_neuron_data(name);
            if (data && i < data->voltage.size()) {
                out << "," << std::setprecision(2) << data->voltage[i]
                    << "," << std::setprecision(3) << data->release[i];
            } else {
                out << ",,-";
            }
        }
        out << "\n";
    }
}

}  // namespace celegans
