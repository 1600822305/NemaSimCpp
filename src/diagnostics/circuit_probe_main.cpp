#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <map>
#include <algorithm>
#include <numeric>

using namespace celegans;

struct CircuitNode {
    std::string name;
    int neuron_id;
    std::vector<double> voltage_trace;
    std::vector<double> release_trace;
    std::vector<double> time_points;
};

struct ConnectionInfo {
    std::string from;
    std::string to;
    std::string type;  // "chem_exc", "chem_inh", "gap"
    double weight;
    double delay_ms;
    double gain;       // output_delta / input_delta
    bool is_active;
};

void print_usage() {
    std::cout << "Usage: circuit_probe <neuron1> <neuron2> ... [options]\n\n"
              << "Trace signal propagation through a neural circuit\n\n"
              << "Options:\n"
              << "  --duration <sec>     Simulation duration (default: 10)\n"
              << "  --sample-rate <ms>   Sampling interval (default: 10)\n"
              << "  --help / -h          Show this help\n\n"
              << "Examples:\n"
              << "  circuit_probe ASEL AIAL AIBL AVAL\n"
              << "  circuit_probe SMDDL SMDVL --duration 5\n"
              << "  circuit_probe ASEL ASER AIY RIM AVA --sample-rate 5\n\n"
              << "Output:\n"
              << "  - Signal timing for each neuron in the path\n"
              << "  - Connection strength and delay between nodes\n"
              << "  - Signal gain/attenuation analysis\n";
}

int main(int argc, char* argv[]) {
    Logger::instance().set_level(LogLevel::ERROR);
    
    if (argc < 2) {
        print_usage();
        return 1;
    }
    
    std::vector<std::string> circuit_path;
    double duration_s = 10.0;
    double sample_interval_ms = 10.0;
    
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--duration" && i+1 < argc) {
            duration_s = std::atof(argv[++i]);
        } else if (arg == "--sample-rate" && i+1 < argc) {
            sample_interval_ms = std::atof(argv[++i]);
        } else if (arg[0] != '-') {
            circuit_path.push_back(arg);
        }
    }
    
    if (circuit_path.empty()) {
        std::cerr << "Error: No circuit path specified\n\n";
        print_usage();
        return 1;
    }
    
    std::cout << "========================================\n";
    std::cout << "  Circuit Probe\n";
    std::cout << "========================================\n\n";
    std::cout << "  Circuit Path: ";
    for (size_t i = 0; i < circuit_path.size(); ++i) {
        std::cout << circuit_path[i];
        if (i + 1 < circuit_path.size()) std::cout << " -> ";
    }
    std::cout << "\n";
    std::cout << "  Duration: " << duration_s << " s\n";
    std::cout << "  Sample Rate: " << sample_interval_ms << " ms\n\n";
    std::cout << "  Running... " << std::flush;
    
    // 初始化仿真
    SimulationEngine sim;
    sim.initialize_default();
    sim.set_rng_seed(123);
    
    // 默认环境
    sim.environment().chemical_field().clear();
    Vector2d food{35.0, 25.0};
    sim.environment().chemical_field().add_point_source(food, 1.0);
    sim.environment().soluble_field().add_point_source(food, 0.4);
    sim.reset_transducers();
    
    // 查找神经元 ID
    const auto& conn = sim.connectome();
    std::vector<CircuitNode> nodes(circuit_path.size());
    bool all_found = true;
    
    for (size_t i = 0; i < circuit_path.size(); ++i) {
        int id = conn.get_neuron_id(circuit_path[i]);
        if (id < 0) {
            std::cerr << "\nError: Neuron '" << circuit_path[i] << "' not found\n";
            all_found = false;
        } else {
            nodes[i].name = circuit_path[i];
            nodes[i].neuron_id = id;
        }
    }
    
    if (!all_found) return 1;
    
    // 查找连接
    std::vector<ConnectionInfo> connections;
    for (size_t i = 0; i + 1 < nodes.size(); ++i) {
        int from_id = nodes[i].neuron_id;
        int to_id = nodes[i+1].neuron_id;
        
        ConnectionInfo conn_info;
        conn_info.from = nodes[i].name;
        conn_info.to = nodes[i+1].name;
        conn_info.type = "none";
        conn_info.weight = 0;
        conn_info.is_active = false;
        
        // 查找化学突触
        const auto& synapses = conn.synapses();
        for (const auto& syn : synapses) {
            if (syn.pre_id() == from_id && syn.post_id() == to_id) {
                conn_info.type = syn.is_excitatory() ? "chem_exc" : "chem_inh";
                conn_info.weight = syn.weight();
                conn_info.is_active = true;
                break;
            }
        }
        
        // 查找间隙连接
        if (!conn_info.is_active) {
            const auto& gaps = conn.gap_junctions();
            for (const auto& gap : gaps) {
                if ((gap.neuron_a() == from_id && gap.neuron_b() == to_id) ||
                    (gap.neuron_a() == to_id && gap.neuron_b() == from_id)) {
                    conn_info.type = "gap";
                    conn_info.weight = gap.conductance();
                    conn_info.is_active = true;
                    break;
                }
            }
        }
        
        connections.push_back(conn_info);
    }
    
    // 运行仿真并采样
    double duration_ms = duration_s * 1000.0;
    int total_steps = (int)(duration_ms / sim.dt());
    int sample_interval = (int)(sample_interval_ms / sim.dt());
    
    for (int s = 0; s < total_steps; ++s) {
        sim.step();
        
        if ((s + 1) % sample_interval == 0) {
            double t = sim.current_time();
            const auto& neurons = sim.neurons();
            
            for (auto& node : nodes) {
                if (node.neuron_id < (int)neurons.size()) {
                    double V = neurons[node.neuron_id]->get_membrane_potential();
                    double S = 1.0 / (1.0 + std::exp(-(V - (-35.0)) / 5.0));
                    node.voltage_trace.push_back(V);
                    node.release_trace.push_back(S);
                    node.time_points.push_back(t);
                }
            }
        }
    }
    
    std::cout << "Done!\n\n";
    
    // 分析信号传递
    for (size_t i = 0; i < connections.size(); ++i) {
        auto& conn_info = connections[i];
        
        // 计算时间延迟（找到源神经元和目标神经元的峰值时间）
        auto& src = nodes[i];
        auto& dst = nodes[i+1];
        
        if (!src.release_trace.empty() && !dst.voltage_trace.empty()) {
            // 找源神经元释放峰值
            auto src_max_it = std::max_element(src.release_trace.begin(), src.release_trace.end());
            size_t src_peak_idx = std::distance(src.release_trace.begin(), src_max_it);
            
            // 找目标神经元电压峰值（在源峰值之后）
            size_t dst_peak_idx = src_peak_idx;
            double max_v = dst.voltage_trace[src_peak_idx];
            for (size_t j = src_peak_idx; j < dst.voltage_trace.size(); ++j) {
                if (dst.voltage_trace[j] > max_v) {
                    max_v = dst.voltage_trace[j];
                    dst_peak_idx = j;
                }
            }
            
            if (dst_peak_idx > src_peak_idx) {
                conn_info.delay_ms = dst.time_points[dst_peak_idx] - src.time_points[src_peak_idx];
                
                // 计算增益（输出变化 / 输入变化）
                double src_delta = *src_max_it - *std::min_element(src.release_trace.begin(), src.release_trace.end());
                double dst_delta = *std::max_element(dst.voltage_trace.begin(), dst.voltage_trace.end()) - 
                                   *std::min_element(dst.voltage_trace.begin(), dst.voltage_trace.end());
                conn_info.gain = (src_delta > 0.001) ? (dst_delta / (src_delta * 100.0)) : 0;
            } else {
                conn_info.delay_ms = 0;
                conn_info.gain = 0;
            }
        }
    }
    
    // 输出报告
    std::cout << "========================================\n";
    std::cout << "  CIRCUIT ANALYSIS\n";
    std::cout << "========================================\n\n";
    
    // 节点信息
    std::cout << "Nodes:\n";
    for (const auto& node : nodes) {
        if (!node.voltage_trace.empty()) {
            auto [min_it, max_it] = std::minmax_element(node.voltage_trace.begin(), node.voltage_trace.end());
            double mean_v = std::accumulate(node.voltage_trace.begin(), node.voltage_trace.end(), 0.0) / node.voltage_trace.size();
            double swing = *max_it - *min_it;
            
            double mean_s = std::accumulate(node.release_trace.begin(), node.release_trace.end(), 0.0) / node.release_trace.size();
            
            std::cout << "  " << std::setw(8) << std::left << node.name
                      << "  V: " << std::fixed << std::setprecision(1) << std::setw(6) << mean_v << " mV"
                      << "  swing: " << std::setw(5) << swing << " mV"
                      << "  release: " << std::setprecision(3) << mean_s << "\n";
        }
    }
    
    std::cout << "\nConnections:\n";
    for (const auto& conn : connections) {
        std::cout << "  " << std::setw(8) << std::left << conn.from
                  << " -> " << std::setw(8) << std::left << conn.to
                  << "  [" << std::setw(8) << conn.type << "]";
        
        if (conn.is_active) {
            std::cout << "  weight: " << std::fixed << std::setprecision(2) << std::setw(6) << conn.weight;
            if (conn.delay_ms > 0) {
                std::cout << "  delay: " << std::setprecision(1) << std::setw(5) << conn.delay_ms << " ms";
                std::cout << "  gain: " << std::setprecision(2) << conn.gain;
            }
        } else {
            std::cout << "  [NO CONNECTION FOUND]";
        }
        std::cout << "\n";
    }
    
    // 信号传递质量评估
    std::cout << "\n========================================\n";
    std::cout << "  SIGNAL QUALITY\n";
    std::cout << "========================================\n\n";
    
    int active_connections = 0;
    int broken_connections = 0;
    for (const auto& conn : connections) {
        if (conn.is_active) active_connections++;
        else broken_connections++;
    }
    
    if (broken_connections == 0) {
        std::cout << "  [OK] All connections present (" << active_connections << "/" << connections.size() << ")\n";
    } else {
        std::cout << "  [FAIL] Broken connections: " << broken_connections << "/" << connections.size() << "\n";
    }
    
    // 检查信号是否沿路径传递
    bool signal_flows = true;
    for (size_t i = 1; i < nodes.size(); ++i) {
        double prev_activity = nodes[i-1].release_trace.empty() ? 0 :
            *std::max_element(nodes[i-1].release_trace.begin(), nodes[i-1].release_trace.end());
        double curr_swing = nodes[i].voltage_trace.empty() ? 0 :
            *std::max_element(nodes[i].voltage_trace.begin(), nodes[i].voltage_trace.end()) - 
            *std::min_element(nodes[i].voltage_trace.begin(), nodes[i].voltage_trace.end());
        
        if (prev_activity > 0.1 && curr_swing < 1.0) {
            std::cout << "  [FAIL] Signal blocked at: " << nodes[i-1].name << " -> " << nodes[i].name << "\n";
            signal_flows = false;
        }
    }
    
    if (signal_flows) {
        std::cout << "  [OK] Signal propagates through entire circuit\n";
    }
    
    std::cout << "\n";
    
    return 0;
}
