#pragma once

#include "core/types.h"
#include "neuron/single_compartment.h"
#include "connectome/connectome.h"
#include <string>
#include <vector>
#include <map>
#include <functional>

namespace celegans {

// 神经元时间序列数据
struct NeuronTimeSeries {
    std::string name;
    std::vector<double> voltage;
    std::vector<double> i_syn;
    std::vector<double> i_ext;
    std::vector<double> release;  // sigmoid(V)
    
    double mean_voltage() const;
    double voltage_swing() const;
    double mean_release() const;
};

// 系统级时间序列
struct SystemTimeSeries {
    std::vector<double> time;           // ms
    std::vector<double> speed;          // mm/s
    std::vector<double> curvature;      // /mm
    std::vector<double> heading;        // deg
    std::vector<double> distance;       // mm from target
    std::vector<Vector2d> position;
    
    // 神经调质
    std::vector<double> serotonin;
    std::vector<double> dopamine;
    std::vector<double> octopamine;
    std::vector<double> tyramine;
    
    // 内部状态
    std::vector<double> satiety;
    std::vector<double> sickness;
    std::vector<double> fatigue;
    std::vector<int> is_sleeping;
    
    // 行为事件
    std::vector<double> reversal_times;
    std::vector<double> omega_times;
};

// 诊断追踪器 - 核心类
class DiagnosticTracker {
public:
    DiagnosticTracker();
    ~DiagnosticTracker();
    
    // 配置
    void set_sample_interval_ms(double interval) { sample_interval_ms_ = interval; }
    void add_tracked_neuron(const std::string& name);
    void track_all_neurons();  // 自动追踪所有神经元
    void track_category(const std::string& category);  // sensory/inter/motor
    
    // 采样
    void sample(double time_ms,
                const Connectome& conn,
                const std::vector<std::unique_ptr<Neuron>>& neurons,
                const class SimulationEngine& sim);
    
    // 数据访问
    const NeuronTimeSeries* get_neuron_data(const std::string& name) const;
    const SystemTimeSeries& get_system_data() const { return system_data_; }
    std::vector<std::string> get_tracked_neuron_names() const;
    
    // 统计分析
    struct NeuronStats {
        std::string name;
        double mean_v;
        double v_min, v_max, v_swing;
        double mean_release;
        double mean_i_syn;
        double mean_i_ext;
    };
    NeuronStats compute_stats(const std::string& name) const;
    std::vector<NeuronStats> compute_all_stats() const;
    
    // 行为指标
    struct BehaviorMetrics {
        double ci;                  // chemotaxis index
        double mean_speed;
        double reversal_rate;       // Hz
        double omega_rate;          // Hz
        double omega_per_reversal;
        double near_target_pct;
        double dv_ratio;            // curvature symmetry
    };
    BehaviorMetrics compute_behavior_metrics(Vector2d target, double radius = 5.0) const;
    
    // 清空数据
    void clear();
    
private:
    double sample_interval_ms_ = 100.0;  // 默认 100ms 采样
    double last_sample_time_ = -1.0;
    
    std::map<std::string, NeuronTimeSeries> neuron_data_;
    SystemTimeSeries system_data_;
    
    // 行为事件追踪
    bool prev_reversing_ = false;
    bool prev_omega_ = false;
    
    double sigmoid_release(double V) const;
};

}  // namespace celegans
