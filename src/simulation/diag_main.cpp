#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <numeric>

using namespace celegans;

int main() {
    Logger::instance().set_level(LogLevel::WARN);

    SimulationEngine sim;
    sim.initialize_default();

    // Default: food at (35,35), start at (25,25)
    // For wall-touch test: food{48.0, 25.0}, start at (40,25)
    Vector2d food{35.0, 35.0};

    auto& conn = sim.connectome();

    int asel_id = conn.get_neuron_id("ASEL");
    int aser_id = conn.get_neuron_id("ASER");
    int smddl_id = conn.get_neuron_id("SMDDL");
    int smdvl_id = conn.get_neuron_id("SMDVL");
    int aval_id = conn.get_neuron_id("AVAL");
    int avbl_id = conn.get_neuron_id("AVBL");
    // Step 19b: intermediate neuron tracking for pirouette pathway
    int aiyl_id = conn.get_neuron_id("AIYL");
    int aiyr_id = conn.get_neuron_id("AIYR");
    int aibl_id = conn.get_neuron_id("AIBL");
    int aibr_id = conn.get_neuron_id("AIBR");
    int aial_id = conn.get_neuron_id("AIAL");
    int aiar_id = conn.get_neuron_id("AIAR");
    int awcl_id = conn.get_neuron_id("AWCL");
    int awcr_id = conn.get_neuron_id("AWCR");
    int rial_id = conn.get_neuron_id("RIAL");
    int riar_id = conn.get_neuron_id("RIAR");

    // Accumulators
    std::vector<double> grad_mags, grad_normals, biases;
    std::vector<double> asel_vs, aser_vs, smd_diffs, curvatures;
    std::vector<double> speeds, headings, dists;
    std::vector<double> aiyl_vs, aiyr_vs, aibl_vs, aibr_vs;
    std::vector<double> aial_vs, aiar_vs, awcl_vs, awcr_vs;
    std::vector<double> aval_vs, rial_vs, riar_vs;
    std::vector<double> sht_vs, da_vs, oa_vs, satiety_vs, spd_scale_vs, fmem_vs, dist_vs_time;

    double prev_heading = sim.body().get_head_angle() * 180.0 / 3.14159265;
    double prev_time = 0;
    double heading_rate_sum = 0;
    int heading_rate_count = 0;

    // Run 300 seconds (see multiple foraging cycles)
    double duration = 300000.0;
    int pirouette_count = 0;
    int reversal_count = 0;
    int omega_count = 0;
    bool prev_reversing = false;
    bool prev_omega = false;
    int wall_touch_count = 0;
    int near_food_samples = 0;   // dist < 5mm
    int total_samples = 0;
    int total_steps = (int)(duration / sim.dt());
    int sample_interval = (int)(100.0 / sim.dt()); // every 100ms

    for (int s = 0; s < total_steps; ++s) {
        sim.step();

        if ((s + 1) % sample_interval == 0) {
            auto head = sim.body().get_head_position();
            const auto& neurons = sim.neurons();
            int n = (int)neurons.size();

            // 1. Gradient
            auto grad = sim.environment().chemical_field().gradient(head);
            double grad_mag = std::sqrt(grad.x * grad.x + grad.y * grad.y);

            // 2. Gradient normal
            double heading_rad = sim.body().get_head_angle();
            double grad_normal = -std::sin(heading_rad) * grad.x + std::cos(heading_rad) * grad.y;

            // 3. Bias
            double bias = sim.params.weathervane_gain * grad_normal;
            double clamp = sim.params.bias_clamp;
            if (bias > clamp) bias = clamp;
            if (bias < -clamp) bias = -clamp;

            // 4. Neuron potentials
            double v_asel = (asel_id >= 0 && asel_id < n) ? neurons[asel_id]->get_membrane_potential() : 0;
            double v_aser = (aser_id >= 0 && aser_id < n) ? neurons[aser_id]->get_membrane_potential() : 0;
            double v_smddl = (smddl_id >= 0 && smddl_id < n) ? neurons[smddl_id]->get_membrane_potential() : 0;
            double v_smdvl = (smdvl_id >= 0 && smdvl_id < n) ? neurons[smdvl_id]->get_membrane_potential() : 0;

            // Intermediate neuron potentials
            auto getV = [&](int id) { return (id >= 0 && id < n) ? neurons[id]->get_membrane_potential() : -65.0; };
            aiyl_vs.push_back(getV(aiyl_id)); aiyr_vs.push_back(getV(aiyr_id));
            aibl_vs.push_back(getV(aibl_id)); aibr_vs.push_back(getV(aibr_id));
            aial_vs.push_back(getV(aial_id)); aiar_vs.push_back(getV(aiar_id));
            awcl_vs.push_back(getV(awcl_id)); awcr_vs.push_back(getV(awcr_id));
            aval_vs.push_back(getV(aval_id));
            rial_vs.push_back(getV(rial_id)); riar_vs.push_back(getV(riar_id));

            // 5. Curvature, speed
            double curv = sim.body().segments()[0].curvature;
            double speed = sim.body().get_speed();

            // 6. Heading rate
            double heading_deg = heading_rad * 180.0 / 3.14159265;
            double dt_sec = (sim.current_time() - prev_time) / 1000.0;
            double h_rate = 0;
            if (dt_sec > 0.01) {
                h_rate = (heading_deg - prev_heading) / dt_sec;
                heading_rate_sum += std::abs(h_rate);
                heading_rate_count++;
                // Detect pirouette: heading jump > 30° in 100ms
                if (std::abs(heading_deg - prev_heading) > 30.0) {
                    pirouette_count++;
                }
            }
            prev_heading = heading_deg;
            prev_time = sim.current_time();

            // 7. Distance
            double dx = head.x - food.x;
            double dy = head.y - food.y;
            double dist = std::sqrt(dx * dx + dy * dy);

            // Track near-food time
            total_samples++;
            if (dist < 5.0) near_food_samples++;

            // Track touch/reversal/omega events
            bool cur_rev = sim.is_reversing();
            bool cur_omega = sim.is_omega_turning();
            if (cur_rev && !prev_reversing) reversal_count++;
            if (cur_omega && !prev_omega) omega_count++;
            // Wall proximity check
            if (head.x < 2.0 || head.x > 48.0 || head.y < 2.0 || head.y > 48.0)
                wall_touch_count++;
            prev_reversing = cur_rev;
            prev_omega = cur_omega;

            // Neuromodulation time series
            sht_vs.push_back(sim.neuromodulation().get_concentration("5-HT"));
            da_vs.push_back(sim.neuromodulation().get_concentration("DA"));
            oa_vs.push_back(sim.neuromodulation().get_concentration("OA"));
            satiety_vs.push_back(sim.satiety());
            spd_scale_vs.push_back(sim.neuromodulation().get_speed_scale());
            fmem_vs.push_back(sim.food_memory());
            dist_vs_time.push_back(dist);

            // Store
            grad_mags.push_back(grad_mag);
            grad_normals.push_back(grad_normal);
            biases.push_back(bias);
            asel_vs.push_back(v_asel);
            aser_vs.push_back(v_aser);
            smd_diffs.push_back(v_smddl - v_smdvl);
            curvatures.push_back(curv);
            speeds.push_back(speed);
            headings.push_back(heading_deg);
            dists.push_back(dist);
        }
    }

    // Helper functions
    auto mean = [](const std::vector<double>& v) {
        return std::accumulate(v.begin(), v.end(), 0.0) / v.size();
    };
    auto absmax = [](const std::vector<double>& v) {
        double m = 0;
        for (auto x : v) if (std::abs(x) > m) m = std::abs(x);
        return m;
    };
    auto minmax = [](const std::vector<double>& v) -> std::pair<double,double> {
        double mn = v[0], mx = v[0];
        for (auto x : v) { if (x < mn) mn = x; if (x > mx) mx = x; }
        return {mn, mx};
    };

    std::cout << std::fixed;
    std::cout << "\n========================================" << std::endl;
    std::cout << "  SIGNAL CHAIN DIAGNOSTIC (60s run)" << std::endl;
    std::cout << "========================================\n" << std::endl;

    auto [g_min, g_max] = minmax(grad_mags);
    std::cout << "1. GRADIENT at head:" << std::endl;
    std::cout << "   magnitude: mean=" << std::setprecision(5) << mean(grad_mags)
              << "  range=[" << g_min << ", " << g_max << "] /mm" << std::endl;

    auto [gn_min, gn_max] = minmax(grad_normals);
    std::cout << "\n2. GRADIENT NORMAL (perp to heading):" << std::endl;
    std::cout << "   mean=" << std::setprecision(5) << mean(grad_normals)
              << "  range=[" << gn_min << ", " << gn_max << "]" << std::endl;

    auto [b_min, b_max] = minmax(biases);
    std::cout << "\n3. WEATHERVANE BIAS CURRENT:" << std::endl;
    std::cout << "   mean=" << std::setprecision(3) << mean(biases)
              << " pA  range=[" << b_min << ", " << b_max << "] pA"
              << "  (gain=" << sim.params.weathervane_gain << ", clamp=" << sim.params.bias_clamp << ")" << std::endl;

    std::cout << "\n4. SENSORY NEURONS (ASEL vs ASER):" << std::endl;
    auto [al_min, al_max] = minmax(asel_vs);
    auto [ar_min, ar_max] = minmax(aser_vs);
    std::cout << "   ASEL: mean=" << std::setprecision(2) << mean(asel_vs)
              << " mV  range=[" << al_min << ", " << al_max << "]" << std::endl;
    std::cout << "   ASER: mean=" << std::setprecision(2) << mean(aser_vs)
              << " mV  range=[" << ar_min << ", " << ar_max << "]" << std::endl;
    std::cout << "   L-R diff: " << std::setprecision(3) << (mean(asel_vs) - mean(aser_vs)) << " mV" << std::endl;

    std::cout << "\n5. SMD DIFFERENTIAL (SMDDL - SMDVL):" << std::endl;
    auto [sd_min, sd_max] = minmax(smd_diffs);
    std::cout << "   mean=" << std::setprecision(3) << mean(smd_diffs)
              << " mV  range=[" << sd_min << ", " << sd_max << "]"
              << "  amplitude=" << std::setprecision(2) << (sd_max - sd_min) << " mV" << std::endl;

    std::cout << "\n6. HEAD CURVATURE:" << std::endl;
    auto [c_min, c_max] = minmax(curvatures);
    std::cout << "   mean=" << std::setprecision(4) << mean(curvatures)
              << "  range=[" << c_min << ", " << c_max << "] /mm"
              << "  amplitude=" << (c_max - c_min) << std::endl;

    std::cout << "\n7. SPEED:" << std::endl;
    auto [sp_min, sp_max] = minmax(speeds);
    std::cout << "   mean=" << std::setprecision(4) << mean(speeds)
              << "  range=[" << sp_min << ", " << sp_max << "] mm/s" << std::endl;

    std::cout << "\n8. HEADING:" << std::endl;
    auto [h_min, h_max] = minmax(headings);
    double avg_rate = (heading_rate_count > 0) ? heading_rate_sum / heading_rate_count : 0;
    std::cout << "   range=[" << std::setprecision(1) << h_min << ", " << h_max << "] deg"
              << "  total_sweep=" << (h_max - h_min) << " deg" << std::endl;
    std::cout << "   avg |dtheta/dt|=" << std::setprecision(3) << avg_rate << " deg/s" << std::endl;
    std::cout << "   pirouettes detected: " << pirouette_count << " (" 
              << std::setprecision(2) << pirouette_count / (duration/1000.0) << " Hz)" << std::endl;

    std::cout << "\n10. TOUCH AVOIDANCE:" << std::endl;
    std::cout << "   reversals: " << reversal_count << std::endl;
    std::cout << "   omega turns: " << omega_count << std::endl;
    std::cout << "   wall proximity samples: " << wall_touch_count 
              << " / " << (int)(duration/100.0) << std::endl;

    std::cout << "\n9. DISTANCE TO FOOD:" << std::endl;
    std::cout << "   initial=" << std::setprecision(2) << dists.front()
              << "  final=" << dists.back() << " mm" << std::endl;
    double ci = (dists.front() - dists.back()) / dists.front();
    std::cout << "   CI=" << std::setprecision(3) << ci << std::endl;
    double near_pct = (total_samples > 0) ? 100.0 * near_food_samples / total_samples : 0;
    std::cout << "   time_near_food(<5mm): " << std::setprecision(1) << near_pct << "% ("
              << std::setprecision(1) << near_food_samples * 0.1 << "s / " << duration/1000.0 << "s)" << std::endl;
    std::cout << "   reversal_rate: " << std::setprecision(2) << reversal_count / (duration/1000.0) << " /s ("
              << reversal_count << " total, target ~0.1/s)" << std::endl;

    // Step 19b: Intermediate neuron diagnostic — pirouette pathway
    std::cout << "\n11. PIROUETTE SIGNAL CHAIN:" << std::endl;
    std::cout << "   AWC (OFF): L=" << std::setprecision(2) << mean(awcl_vs)
              << "  R=" << mean(awcr_vs) << " mV" << std::endl;
    std::cout << "   AIA:       L=" << mean(aial_vs) << "  R=" << mean(aiar_vs) << " mV" << std::endl;
    std::cout << "   AIB:       L=" << mean(aibl_vs) << "  R=" << mean(aibr_vs) << " mV" << std::endl;
    std::cout << "   AIY:       L=" << mean(aiyl_vs) << "  R=" << mean(aiyr_vs) << " mV" << std::endl;
    std::cout << "   RIA:       L=" << mean(rial_vs) << "  R=" << mean(riar_vs) << " mV" << std::endl;
    std::cout << "   AVA:       L=" << mean(aval_vs) << " mV" << std::endl;
    // Release rates at V_thresh=-35, slope=5
    auto release = [](double V) { return 1.0 / (1.0 + std::exp(-(V - (-35.0)) / 5.0)); };
    std::cout << "   Release rates (S = sigmoid(V)):" << std::endl;
    std::cout << "     ASEL=" << std::setprecision(3) << release(mean(asel_vs))
              << "  ASER=" << release(mean(aser_vs))
              << "  AWCL=" << release(mean(awcl_vs))
              << "  AWCR=" << release(mean(awcr_vs)) << std::endl;
    std::cout << "     AIAL=" << release(mean(aial_vs))
              << "  AIAR=" << release(mean(aiar_vs))
              << "  AIBL=" << release(mean(aibl_vs))
              << "  AIBR=" << release(mean(aibr_vs)) << std::endl;
    std::cout << "     AVAL=" << release(mean(aval_vs)) << std::endl;

    // Step 20: Neuromodulation diagnostic
    std::cout << "\n12. NEUROMODULATION (Layer 6):" << std::endl;
    const auto& mods = sim.neuromodulation().modulators();
    for (const auto& mod : mods) {
        std::cout << "   " << mod.name << ": conc=" << std::setprecision(4) << mod.concentration
                  << "  sources=" << mod.source_neuron_ids.size()
                  << "  targets=" << mod.targets.size() << std::endl;
    }
    std::cout << "   speed_scale=" << std::setprecision(3) << sim.neuromodulation().get_speed_scale()
              << "  (effective=" << sim.neuromodulation().get_speed_scale() * sim.params.speed_scale << ")" << std::endl;

    // Time series: 5-HT, DA, OA, satiety, distance every 20s
    std::cout << "   Time series (every 20s):" << std::endl;
    std::cout << "     t(s)  dist   5-HT   DA    OA    sat   fmem  spd" << std::endl;
    int samples_per_20s = (int)(20000.0 / 100.0); // 200 samples per 20s
    for (int t = 0; t < 15; ++t) {
        int idx = (t + 1) * samples_per_20s - 1;
        if (idx < (int)sht_vs.size()) {
            std::cout << "     " << std::setw(4) << (t + 1) * 20 << "  "
                      << std::setprecision(2) << std::setw(5) << dist_vs_time[idx] << "  "
                      << std::setprecision(3) << std::setw(5) << sht_vs[idx] << "  "
                      << std::setw(5) << da_vs[idx] << "  "
                      << std::setw(5) << oa_vs[idx] << "  "
                      << std::setw(5) << satiety_vs[idx] << "  "
                      << std::setw(5) << fmem_vs[idx] << "  "
                      << std::setprecision(3) << spd_scale_vs[idx] << std::endl;
        }
    }

    // BOTTLENECK ANALYSIS
    std::cout << "\n========================================" << std::endl;
    std::cout << "  BOTTLENECK ANALYSIS" << std::endl;
    std::cout << "========================================\n" << std::endl;

    bool has_bottleneck = false;

    if (mean(grad_mags) < 0.005) {
        std::cout << "  [!!] GRADIENT too small (" << mean(grad_mags) << " /mm)" << std::endl;
        std::cout << "       -> Worm may be too far from food, or sigma too large" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] Gradient magnitude adequate" << std::endl;
    }

    if (absmax(biases) < 0.5) {
        std::cout << "  [!!] BIAS CURRENT too weak (max " << absmax(biases) << " pA)" << std::endl;
        std::cout << "       -> Increase weathervane_gain (try 200-500)" << std::endl;
        has_bottleneck = true;
    } else if (absmax(biases) >= sim.params.bias_clamp * 0.9) {
        std::cout << "  [!!] BIAS CURRENT hitting clamp (" << sim.params.bias_clamp << " pA)" << std::endl;
        std::cout << "       -> Increase bias_clamp (try 20-50)" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] Bias current adequate" << std::endl;
    }

    double smd_amp = sd_max - sd_min;
    if (smd_amp < 2.0) {
        std::cout << "  [!!] SMD DIFFERENTIAL too small (" << smd_amp << " mV)" << std::endl;
        std::cout << "       -> Increase synapse_scale or weathervane_gain" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] SMD differential " << smd_amp << " mV" << std::endl;
    }

    double curv_amp = c_max - c_min;
    if (curv_amp < 0.1) {
        std::cout << "  [!!] CURVATURE too small (" << curv_amp << " /mm)" << std::endl;
        std::cout << "       -> Check muscle_gain in body_model or synapse_scale" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] Curvature amplitude " << curv_amp << " /mm" << std::endl;
    }

    if (mean(speeds) < 0.1) {
        std::cout << "  [!!] SPEED too slow (" << mean(speeds) << " mm/s)" << std::endl;
        std::cout << "       -> Increase speed_scale" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] Speed " << mean(speeds) << " mm/s" << std::endl;
    }

    if (avg_rate < 1.0) {
        std::cout << "  [!!] HEADING RATE too low (" << avg_rate << " deg/s, need 1-5)" << std::endl;
        std::cout << "       -> Product of speed * curvature too small" << std::endl;
        has_bottleneck = true;
    } else {
        std::cout << "  [OK] Heading rate " << avg_rate << " deg/s" << std::endl;
    }

    if (ci < 0.3) {
        std::cout << "  [!!] CI poor (" << ci << ", target >0.5)" << std::endl;
        has_bottleneck = true;
    } else if (ci >= 0.5) {
        std::cout << "  [OK] CI good (" << ci << " >= 0.5)" << std::endl;
    } else {
        std::cout << "  [..] CI moderate (" << ci << ", target >0.5)" << std::endl;
    }

    if (!has_bottleneck) {
        std::cout << "\n  All stages look healthy!" << std::endl;
    }

    std::cout << std::endl;
    return 0;
}
