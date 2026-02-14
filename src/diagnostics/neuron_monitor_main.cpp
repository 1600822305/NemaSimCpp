#include "diagnostics/diagnostic_tracker.h"
#include "diagnostics/output_formatter.h"
#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <fstream>
#include <string>
#include <vector>

using namespace celegans;

struct AnalyzerConfig {
    double duration_s = 60.0;
    unsigned int seed = 123;
    bool track_all = false;
    std::vector<std::string> tracked_neurons;
    std::string output_format = "text";  // text/json/csv
    std::string export_file;
    bool no_toxin = false;
    bool quiet = false;
    Vector2d food_pos{35.0, 25.0};
};

AnalyzerConfig parse_args(int argc, char* argv[]) {
    AnalyzerConfig cfg;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--duration" && i+1 < argc) {
            cfg.duration_s = std::atof(argv[++i]);
        }
        else if (arg == "--seed" && i+1 < argc) {
            cfg.seed = std::atoi(argv[++i]);
        }
        else if (arg == "--track-all") {
            cfg.track_all = true;
        }
        else if (arg == "--track" && i+1 < argc) {
            cfg.tracked_neurons.push_back(argv[++i]);
        }
        else if (arg == "--format" && i+1 < argc) {
            cfg.output_format = argv[++i];
        }
        else if (arg == "--export" && i+1 < argc) {
            cfg.export_file = argv[++i];
        }
        else if (arg == "--no-toxin") {
            cfg.no_toxin = true;
        }
        else if (arg == "--quiet" || arg == "-q") {
            cfg.quiet = true;
        }
        else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: neuron_monitor [options]\n\n"
                      << "Options:\n"
                      << "  --duration <sec>     Simulation duration (default: 60)\n"
                      << "  --seed <n>           RNG seed (default: 123)\n"
                      << "  --track-all          Track all neurons automatically\n"
                      << "  --track <name>       Track specific neuron (repeatable)\n"
                      << "  --format <fmt>       Output format: text/json/csv (default: text)\n"
                      << "  --export <file>      Export time series to CSV file\n"
                      << "  --no-toxin           Disable toxic food\n"
                      << "  --quiet / -q         Minimal output\n"
                      << "  --help / -h          Show this help\n\n"
                      << "Examples:\n"
                      << "  neuron_monitor --duration 300\n"
                      << "  neuron_monitor --track-all --format json\n"
                      << "  neuron_monitor --track ASEL --track ASER --export data.csv\n";
            std::exit(0);
        }
    }
    
    return cfg;
}

int main(int argc, char* argv[]) {
    Logger::instance().set_level(LogLevel::WARN);
    
    AnalyzerConfig cfg = parse_args(argc, argv);
    
    if (!cfg.quiet) {
        std::cout << "========================================\n";
        std::cout << "  Neuron Monitor\n";
        std::cout << "========================================\n\n";
        std::cout << "  Duration:  " << cfg.duration_s << " s\n";
        std::cout << "  Seed:      " << cfg.seed << "\n";
        std::cout << "  Track:     " << (cfg.track_all ? "All neurons" : 
                                         std::to_string(cfg.tracked_neurons.size()) + " specific") << "\n";
        std::cout << "  Format:    " << cfg.output_format << "\n\n";
    }
    
    // 初始化仿真
    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(cfg.seed);
    
    // 环境设置
    sim.environment().chemical_field().clear();
    sim.environment().chemical_field().add_point_source(cfg.food_pos, 1.0);
    sim.environment().soluble_field().add_point_source(cfg.food_pos, 0.4);
    
    if (!cfg.no_toxin) {
        sim.environment().repellent_field().add_point_source(cfg.food_pos, 0.8, 25.0);
    }
    sim.reset_transducers();
    
    // 配置诊断追踪器
    DiagnosticTracker tracker;
    tracker.set_sample_interval_ms(100.0);
    
    if (cfg.track_all) {
        tracker.track_all_neurons();
    } else if (!cfg.tracked_neurons.empty()) {
        for (const auto& name : cfg.tracked_neurons) {
            tracker.add_tracked_neuron(name);
        }
    } else {
        // 默认追踪关键神经元
        std::vector<std::string> defaults = {
            "ASEL", "ASER", "AWCL", "AWCR",
            "AIAL", "AIAR", "AIBL", "AIBR", "AIYL", "AIYR",
            "AVAL", "AVAR", "AVBL", "AVBR",
            "SMDDL", "SMDDR", "SMDVL", "SMDVR"
        };
        for (const auto& name : defaults) {
            tracker.add_tracked_neuron(name);
        }
    }
    
    // 运行仿真
    double duration_ms = cfg.duration_s * 1000.0;
    int total_steps = (int)(duration_ms / sim.dt());
    
    if (!cfg.quiet) {
        std::cout << "Running simulation... " << std::flush;
    }
    
    for (int s = 0; s < total_steps; ++s) {
        sim.step();
        
        // 采样诊断数据
        tracker.sample(sim.current_time(), sim.connectome(), sim.neurons(), sim);
        
        // 进度指示
        if (!cfg.quiet && (s + 1) % (total_steps / 10) == 0) {
            std::cout << "." << std::flush;
        }
    }
    
    if (!cfg.quiet) {
        std::cout << " Done!\n\n";
    }
    
    // 输出格式
    OutputFormatter::Format fmt = OutputFormatter::Format::TEXT;
    if (cfg.output_format == "json") {
        fmt = OutputFormatter::Format::JSON;
    } else if (cfg.output_format == "csv") {
        fmt = OutputFormatter::Format::CSV;
    }
    
    // 打印结果
    auto behavior = tracker.compute_behavior_metrics(cfg.food_pos, 5.0);
    OutputFormatter::print_behavior_metrics(std::cout, behavior, fmt);
    
    auto neuron_stats = tracker.compute_all_stats();
    OutputFormatter::print_neuron_stats(std::cout, neuron_stats, fmt);
    
    if (fmt == OutputFormatter::Format::TEXT) {
        OutputFormatter::print_system_summary(std::cout, tracker, fmt);
    }
    
    // 导出时间序列
    if (!cfg.export_file.empty()) {
        std::ofstream ofs(cfg.export_file);
        if (ofs) {
            auto names = tracker.get_tracked_neuron_names();
            OutputFormatter::export_time_series(ofs, tracker, names);
            if (!cfg.quiet) {
                std::cout << "\nTime series exported to: " << cfg.export_file << "\n";
            }
        } else {
            std::cerr << "ERROR: Cannot open file: " << cfg.export_file << "\n";
        }
    }
    
    return 0;
}
