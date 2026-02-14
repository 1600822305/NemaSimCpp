#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>

using namespace celegans;

struct NeuronHealth {
    std::string name;
    double mean_v;
    double v_swing;
    double mean_release;
    bool is_alive;      // V > -60mV
    bool is_active;     // release > 0.01
    bool is_oscillating; // swing > 50mV (可能异常)
};

int main(int argc, char* argv[]) {
    Logger::instance().set_level(LogLevel::ERROR);
    
    bool verbose = false;
    double duration_s = 30.0;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--verbose" || arg == "-v") {
            verbose = true;
        } else if (arg == "--duration" && i+1 < argc) {
            duration_s = std::atof(argv[++i]);
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: health_check [options]\n\n"
                      << "Quick health check for all 302 neurons (default: 30s simulation)\n\n"
                      << "Options:\n"
                      << "  --verbose / -v       Show detailed neuron list\n"
                      << "  --duration <sec>     Simulation duration (default: 30)\n"
                      << "  --help / -h          Show this help\n\n"
                      << "Output:\n"
                      << "  Alive:      V > -60mV (not dead)\n"
                      << "  Active:     release > 0.01 (has output)\n"
                      << "  Oscillating: swing > 50mV (possible anomaly)\n";
            return 0;
        }
    }
    
    std::cout << "========================================\n";
    std::cout << "  Neuron Health Check\n";
    std::cout << "========================================\n\n";
    std::cout << "  Duration: " << duration_s << " s\n";
    std::cout << "  Running... " << std::flush;
    
    // 初始化仿真
    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(123);
    
    // 默认环境（食物）
    sim.environment().chemical_field().clear();
    Vector2d food{35.0, 25.0};
    sim.environment().chemical_field().add_point_source(food, 1.0);
    sim.environment().soluble_field().add_point_source(food, 0.4);
    sim.reset_transducers();
    
    // 运行仿真并采样
    double duration_ms = duration_s * 1000.0;
    int total_steps = (int)(duration_ms / sim.dt());
    int sample_interval = (int)(100.0 / sim.dt());
    
    const auto& conn = sim.connectome();
    const auto& neuron_infos = conn.neuron_infos();
    std::vector<NeuronHealth> health_data(neuron_infos.size());
    
    // 初始化
    for (size_t i = 0; i < neuron_infos.size(); ++i) {
        health_data[i].name = neuron_infos[i].name;
        health_data[i].mean_v = 0;
        health_data[i].v_swing = 0;
        health_data[i].mean_release = 0;
    }
    
    std::vector<std::vector<double>> voltage_samples(neuron_infos.size());
    std::vector<std::vector<double>> release_samples(neuron_infos.size());
    
    for (int s = 0; s < total_steps; ++s) {
        sim.step();
        
        if ((s + 1) % sample_interval == 0) {
            const auto& neurons = sim.neurons();
            for (size_t i = 0; i < neurons.size() && i < neuron_infos.size(); ++i) {
                double V = neurons[i]->get_membrane_potential();
                double S = 1.0 / (1.0 + std::exp(-(V - (-35.0)) / 5.0));
                voltage_samples[i].push_back(V);
                release_samples[i].push_back(S);
            }
        }
    }
    
    std::cout << "Done!\n\n";
    
    // 计算统计量
    for (size_t i = 0; i < health_data.size(); ++i) {
        auto& h = health_data[i];
        
        if (!voltage_samples[i].empty()) {
            // 均值
            double sum_v = 0, sum_s = 0;
            for (double v : voltage_samples[i]) sum_v += v;
            for (double s : release_samples[i]) sum_s += s;
            h.mean_v = sum_v / voltage_samples[i].size();
            h.mean_release = sum_s / release_samples[i].size();
            
            // 摆幅
            auto [min_it, max_it] = std::minmax_element(voltage_samples[i].begin(), 
                                                         voltage_samples[i].end());
            h.v_swing = *max_it - *min_it;
            
            // 健康判断
            h.is_alive = h.mean_v > -60.0;
            h.is_active = h.mean_release > 0.01;
            h.is_oscillating = h.v_swing > 50.0;
        }
    }
    
    // 统计
    int total = health_data.size();
    int alive = 0, active = 0, oscillating = 0;
    std::vector<std::string> dead_neurons, silent_neurons, anomalous_neurons;
    
    for (const auto& h : health_data) {
        if (h.is_alive) alive++;
        if (h.is_active) active++;
        if (h.is_oscillating) oscillating++;
        
        if (!h.is_alive) dead_neurons.push_back(h.name);
        if (h.is_alive && !h.is_active) silent_neurons.push_back(h.name);
        if (h.is_oscillating) anomalous_neurons.push_back(h.name);
    }
    
    // 输出报告
    std::cout << "========================================\n";
    std::cout << "  SUMMARY\n";
    std::cout << "========================================\n\n";
    std::cout << "  Total Neurons:    " << total << "\n";
    std::cout << "  Alive:            " << alive << " (" << (100.0 * alive / total) << "%)\n";
    std::cout << "  Active:           " << active << " (" << (100.0 * active / total) << "%)\n";
    std::cout << "  Silent (alive but inactive): " << silent_neurons.size() << "\n";
    std::cout << "  Dead (V < -60mV): " << dead_neurons.size() << "\n";
    std::cout << "  Oscillating (swing > 50mV): " << oscillating << "\n\n";
    
    // 健康状态
    if (dead_neurons.empty() && anomalous_neurons.empty()) {
        std::cout << "  ✓ Status: HEALTHY\n";
        std::cout << "    All neurons alive, no anomalies detected.\n\n";
    } else {
        std::cout << "  ✗ Status: ISSUES DETECTED\n\n";
        
        if (!dead_neurons.empty()) {
            std::cout << "  Dead Neurons (" << dead_neurons.size() << "):\n";
            for (size_t i = 0; i < std::min(dead_neurons.size(), size_t(10)); ++i) {
                std::cout << "    - " << dead_neurons[i] << "\n";
            }
            if (dead_neurons.size() > 10) {
                std::cout << "    ... and " << (dead_neurons.size() - 10) << " more\n";
            }
            std::cout << "\n";
        }
        
        if (!anomalous_neurons.empty()) {
            std::cout << "  Anomalous Oscillations (" << anomalous_neurons.size() << "):\n";
            for (size_t i = 0; i < std::min(anomalous_neurons.size(), size_t(10)); ++i) {
                auto it = std::find_if(health_data.begin(), health_data.end(),
                    [&](const NeuronHealth& h) { return h.name == anomalous_neurons[i]; });
                if (it != health_data.end()) {
                    std::cout << "    - " << it->name << " (swing: " 
                              << std::fixed << std::setprecision(1) << it->v_swing << " mV)\n";
                }
            }
            if (anomalous_neurons.size() > 10) {
                std::cout << "    ... and " << (anomalous_neurons.size() - 10) << " more\n";
            }
            std::cout << "\n";
        }
    }
    
    // Verbose 模式：显示所有神经元
    if (verbose) {
        std::cout << "========================================\n";
        std::cout << "  DETAILED NEURON LIST\n";
        std::cout << "========================================\n\n";
        std::cout << std::left << std::setw(12) << "Neuron"
                  << std::right << std::setw(10) << "Mean V"
                  << std::setw(10) << "Swing"
                  << std::setw(10) << "Release"
                  << std::setw(10) << "Status" << "\n";
        std::cout << std::string(52, '-') << "\n";
        
        for (const auto& h : health_data) {
            std::string status;
            if (!h.is_alive) status = "DEAD";
            else if (h.is_oscillating) status = "OSCILLATE";
            else if (!h.is_active) status = "SILENT";
            else status = "OK";
            
            std::cout << std::left << std::setw(12) << h.name
                      << std::right << std::fixed << std::setprecision(1)
                      << std::setw(10) << h.mean_v
                      << std::setw(10) << h.v_swing
                      << std::setprecision(3)
                      << std::setw(10) << h.mean_release
                      << std::setw(10) << status << "\n";
        }
    }
    
    return 0;
}
