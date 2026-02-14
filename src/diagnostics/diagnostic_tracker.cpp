#include "diagnostics/diagnostic_tracker.h"
#include "simulation/simulation_engine.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace celegans {

// ============================================================================
// NeuronTimeSeries 实现
// ============================================================================

double NeuronTimeSeries::mean_voltage() const {
    if (voltage.empty()) return -65.0;
    return std::accumulate(voltage.begin(), voltage.end(), 0.0) / voltage.size();
}

double NeuronTimeSeries::voltage_swing() const {
    if (voltage.empty()) return 0.0;
    auto [min_it, max_it] = std::minmax_element(voltage.begin(), voltage.end());
    return *max_it - *min_it;
}

double NeuronTimeSeries::mean_release() const {
    if (release.empty()) return 0.0;
    return std::accumulate(release.begin(), release.end(), 0.0) / release.size();
}

// ============================================================================
// DiagnosticTracker 实现
// ============================================================================

DiagnosticTracker::DiagnosticTracker() {}

DiagnosticTracker::~DiagnosticTracker() {}

void DiagnosticTracker::add_tracked_neuron(const std::string& name) {
    if (neuron_data_.find(name) == neuron_data_.end()) {
        neuron_data_[name] = NeuronTimeSeries{name, {}, {}, {}, {}};
    }
}

void DiagnosticTracker::track_all_neurons() {
    // 将在 sample() 时自动发现所有神经元
}

void DiagnosticTracker::track_category(const std::string& category) {
    // TODO: 根据类别过滤（需要 Connectome 提供神经元分类 API）
}

void DiagnosticTracker::sample(double time_ms,
                                const Connectome& conn,
                                const std::vector<std::unique_ptr<Neuron>>& neurons,
                                const SimulationEngine& sim) {
    // 检查采样间隔
    if (last_sample_time_ >= 0 && (time_ms - last_sample_time_) < sample_interval_ms_) {
        return;
    }
    last_sample_time_ = time_ms;
    
    // 采样系统状态
    system_data_.time.push_back(time_ms);
    system_data_.speed.push_back(sim.body().get_speed());
    system_data_.curvature.push_back(sim.body().segments()[0].curvature);
    system_data_.heading.push_back(sim.body().get_head_angle() * 180.0 / 3.14159265);
    system_data_.position.push_back(sim.body().get_head_position());
    
    // 神经调质
    system_data_.serotonin.push_back(sim.neuromodulation().get_concentration("5-HT"));
    system_data_.dopamine.push_back(sim.neuromodulation().get_concentration("DA"));
    system_data_.octopamine.push_back(sim.neuromodulation().get_concentration("OA"));
    system_data_.tyramine.push_back(sim.neuromodulation().get_concentration("TA"));
    
    // 内部状态
    system_data_.satiety.push_back(sim.satiety());
    system_data_.sickness.push_back(sim.sickness());
    system_data_.fatigue.push_back(sim.fatigue());
    system_data_.is_sleeping.push_back(sim.is_sleeping() ? 1 : 0);
    
    // 行为事件检测
    bool cur_rev = sim.is_reversing();
    bool cur_omega = sim.is_omega_turning();
    if (cur_rev && !prev_reversing_) {
        system_data_.reversal_times.push_back(time_ms);
    }
    if (cur_omega && !prev_omega_) {
        system_data_.omega_times.push_back(time_ms);
    }
    prev_reversing_ = cur_rev;
    prev_omega_ = cur_omega;
    
    // 采样神经元
    const auto& neuron_infos = conn.neuron_infos();
    for (size_t i = 0; i < neurons.size() && i < neuron_infos.size(); ++i) {
        const std::string& name = neuron_infos[i].name;
        
        // 自动添加追踪（如果是 track_all 模式）
        if (neuron_data_.find(name) == neuron_data_.end()) {
            neuron_data_[name] = NeuronTimeSeries{name, {}, {}, {}, {}};
        }
        
        auto& data = neuron_data_[name];
        double V = neurons[i]->get_membrane_potential();
        data.voltage.push_back(V);
        data.i_syn.push_back(neurons[i]->get_I_syn());
        data.i_ext.push_back(neurons[i]->get_I_ext());
        data.release.push_back(sigmoid_release(V));
    }
}

const NeuronTimeSeries* DiagnosticTracker::get_neuron_data(const std::string& name) const {
    auto it = neuron_data_.find(name);
    return (it != neuron_data_.end()) ? &it->second : nullptr;
}

std::vector<std::string> DiagnosticTracker::get_tracked_neuron_names() const {
    std::vector<std::string> names;
    names.reserve(neuron_data_.size());
    for (const auto& [name, _] : neuron_data_) {
        names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

DiagnosticTracker::NeuronStats DiagnosticTracker::compute_stats(const std::string& name) const {
    auto it = neuron_data_.find(name);
    if (it == neuron_data_.end()) {
        return {name, -65.0, -65.0, -65.0, 0.0, 0.0, 0.0, 0.0};
    }
    
    const auto& data = it->second;
    NeuronStats stats;
    stats.name = name;
    stats.mean_v = data.mean_voltage();
    stats.mean_release = data.mean_release();
    
    if (!data.voltage.empty()) {
        auto [min_it, max_it] = std::minmax_element(data.voltage.begin(), data.voltage.end());
        stats.v_min = *min_it;
        stats.v_max = *max_it;
        stats.v_swing = stats.v_max - stats.v_min;
    }
    
    if (!data.i_syn.empty()) {
        stats.mean_i_syn = std::accumulate(data.i_syn.begin(), data.i_syn.end(), 0.0) / data.i_syn.size();
    }
    if (!data.i_ext.empty()) {
        stats.mean_i_ext = std::accumulate(data.i_ext.begin(), data.i_ext.end(), 0.0) / data.i_ext.size();
    }
    
    return stats;
}

std::vector<DiagnosticTracker::NeuronStats> DiagnosticTracker::compute_all_stats() const {
    std::vector<NeuronStats> all_stats;
    all_stats.reserve(neuron_data_.size());
    
    for (const auto& [name, _] : neuron_data_) {
        all_stats.push_back(compute_stats(name));
    }
    
    // 按名称排序
    std::sort(all_stats.begin(), all_stats.end(),
              [](const NeuronStats& a, const NeuronStats& b) { return a.name < b.name; });
    
    return all_stats;
}

DiagnosticTracker::BehaviorMetrics DiagnosticTracker::compute_behavior_metrics(
    Vector2d target, double radius) const {
    
    BehaviorMetrics metrics{};
    
    if (system_data_.position.empty()) return metrics;
    
    // CI (chemotaxis index)
    Vector2d first_pos = system_data_.position.front();
    Vector2d last_pos = system_data_.position.back();
    double dx1 = first_pos.x - target.x, dy1 = first_pos.y - target.y;
    double dx2 = last_pos.x - target.x, dy2 = last_pos.y - target.y;
    double dist_initial = std::sqrt(dx1*dx1 + dy1*dy1);
    double dist_final = std::sqrt(dx2*dx2 + dy2*dy2);
    metrics.ci = (dist_initial > 0) ? (dist_initial - dist_final) / dist_initial : 0.0;
    
    // 速度
    if (!system_data_.speed.empty()) {
        metrics.mean_speed = std::accumulate(system_data_.speed.begin(),
                                             system_data_.speed.end(), 0.0) / system_data_.speed.size();
    }
    
    // Reversal/Omega 频率
    double duration_s = system_data_.time.empty() ? 0 :
                        (system_data_.time.back() - system_data_.time.front()) / 1000.0;
    if (duration_s > 0) {
        metrics.reversal_rate = system_data_.reversal_times.size() / duration_s;
        metrics.omega_rate = system_data_.omega_times.size() / duration_s;
    }
    metrics.omega_per_reversal = (system_data_.reversal_times.size() > 0) ?
        (double)system_data_.omega_times.size() / system_data_.reversal_times.size() : 0.0;
    
    // Near target percentage
    int near_count = 0;
    for (const auto& pos : system_data_.position) {
        double dx = pos.x - target.x, dy = pos.y - target.y;
        if (std::sqrt(dx*dx + dy*dy) < radius) near_count++;
    }
    metrics.near_target_pct = 100.0 * near_count / system_data_.position.size();
    
    // D/V 对称性（曲率比）
    if (!system_data_.curvature.empty()) {
        auto [min_it, max_it] = std::minmax_element(system_data_.curvature.begin(),
                                                     system_data_.curvature.end());
        double c_max = *max_it, c_min = *min_it;
        metrics.dv_ratio = (std::abs(c_min) > 0.001) ? std::abs(c_max) / std::abs(c_min) : 99.9;
    }
    
    return metrics;
}

void DiagnosticTracker::clear() {
    neuron_data_.clear();
    system_data_ = SystemTimeSeries{};
    prev_reversing_ = false;
    prev_omega_ = false;
    last_sample_time_ = -1.0;
}

double DiagnosticTracker::sigmoid_release(double V) const {
    return 1.0 / (1.0 + std::exp(-(V - (-35.0)) / 5.0));
}

}  // namespace celegans
