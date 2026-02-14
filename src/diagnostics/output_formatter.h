#pragma once

#include "diagnostics/diagnostic_tracker.h"
#include <string>
#include <ostream>

namespace celegans {

class OutputFormatter {
public:
    enum class Format {
        TEXT,    // 人类可读文本
        JSON,    // 机器可读JSON
        CSV      // 表格数据
    };
    
    static void print_neuron_stats(std::ostream& out,
                                   const std::vector<DiagnosticTracker::NeuronStats>& stats,
                                   Format format = Format::TEXT);
    
    static void print_behavior_metrics(std::ostream& out,
                                       const DiagnosticTracker::BehaviorMetrics& metrics,
                                       Format format = Format::TEXT);
    
    static void print_system_summary(std::ostream& out,
                                     const DiagnosticTracker& tracker,
                                     Format format = Format::TEXT);
    
    static void export_time_series(std::ostream& out,
                                   const DiagnosticTracker& tracker,
                                   const std::vector<std::string>& neuron_names);
};

}  // namespace celegans
