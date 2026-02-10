#include "simulation/simulation_engine.h"
#include "compute/compute_backend.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <cmath>
#include <vector>
#include <numeric>
#ifdef _OPENMP
#include <omp.h>
#endif

using namespace celegans;

int main() {
    Logger::instance().set_level(LogLevel::WARN);

    // GPU device detection
    {
        auto devices = ComputeBackend::enumerate_devices();
        std::cout << "========================================" << std::endl;
        std::cout << "  COMPUTE DEVICES" << std::endl;
        std::cout << "========================================\n" << std::endl;
        if (devices.empty()) {
            std::cout << "  No OpenCL devices found" << std::endl;
        }
        for (size_t i = 0; i < devices.size(); ++i) {
            auto& d = devices[i];
            std::cout << "  [" << i << "] " << d.name
                      << (d.is_gpu ? " (GPU)" : " (CPU)")
                      << "\n      " << d.vendor << " | " << d.version
                      << "\n      " << d.global_mem_bytes / (1024*1024) << " MB | "
                      << d.max_compute_units << " CUs | "
                      << "WG " << d.max_work_group_size << std::endl;
        }
        bool has_gpu = ComputeBackend::opencl_available();
        std::cout << "\n  GPU available: " << (has_gpu ? "YES" : "NO")
                  << "  |  Simulation uses: CPU"
                  << "\n  (GPU auto-enables at >500 synapses; current ~110)"
                  << "\n" << std::endl;
    }

    SimulationEngine sim;
    sim.initialize_default();

    // Default: food at (35,35), start at (25,25)
    // Step 25: repellent at (30,30) — between start and food, blocking direct path
    Vector2d food{35.0, 35.0};
    Vector2d repellent{30.0, 30.0};
    // σ²=25mm² (σ≈5mm): localized toxin, not wide diffusion like attractant σ²=144
    // At 5mm: C=0.8×exp(-25/50)=0.49, at 8mm: C=0.8×exp(-64/50)=0.22, at 12mm: C≈0.06
    sim.environment().repellent_field().add_point_source(repellent, 0.8, 25.0);

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
    int ashl_id = conn.get_neuron_id("ASHL");
    int ashr_id = conn.get_neuron_id("ASHR");

    // Accumulators
    std::vector<double> grad_mags, grad_normals, biases;
    std::vector<double> asel_vs, aser_vs, smd_diffs, curvatures;
    std::vector<double> speeds, headings, dists;
    std::vector<double> aiyl_vs, aiyr_vs, aibl_vs, aibr_vs;
    std::vector<double> aial_vs, aiar_vs, awcl_vs, awcr_vs;
    std::vector<double> aval_vs, rial_vs, riar_vs;
    std::vector<double> sht_vs, da_vs, oa_vs, satiety_vs, spd_scale_vs, fmem_vs, dist_vs_time, xpos_vs;
    std::vector<double> pump_rate_vs, pharynx_v_vs;  // Step 24: pharyngeal diagnostics
    // SMD current diagnostics
    std::vector<double> smddl_v_vs, smdvl_v_vs, smddl_isyn_vs, smddl_iext_vs;

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

            // SMD current diagnostics
            if (smddl_id >= 0 && smddl_id < n) {
                smddl_v_vs.push_back(v_smddl);
                smdvl_v_vs.push_back(v_smdvl);
                smddl_isyn_vs.push_back(neurons[smddl_id]->get_I_syn());
                smddl_iext_vs.push_back(neurons[smddl_id]->get_I_ext());
            }

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
            xpos_vs.push_back(head.x);
            pump_rate_vs.push_back(sim.pump_rate_hz());
            pharynx_v_vs.push_back(sim.pharynx_V());

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
    // Individual SMDDL/SMDVL ranges
    if (!smddl_v_vs.empty()) {
        auto [dl_min, dl_max] = minmax(smddl_v_vs);
        auto [vl_min, vl_max] = minmax(smdvl_v_vs);
        auto [is_min, is_max] = minmax(smddl_isyn_vs);
        auto [ie_min, ie_max] = minmax(smddl_iext_vs);
        std::cout << "   SMDDL V: [" << dl_min << ", " << dl_max << "] swing=" << (dl_max-dl_min) << " mV" << std::endl;
        std::cout << "   SMDVL V: [" << vl_min << ", " << vl_max << "] swing=" << (vl_max-vl_min) << " mV" << std::endl;
        std::cout << "   SMDDL I_syn: [" << is_min << ", " << is_max << "] pA" << std::endl;
        std::cout << "   SMDDL I_ext: [" << ie_min << ", " << ie_max << "] pA" << std::endl;
    }
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
    // Pirouette = reversal (± omega turn). Old heading-jump detection (>30°/100ms)
    // doesn't work with smooth curvature_bias omega turns (~9.6°/100ms).
    std::cout << "   pirouettes (=reversals): " << reversal_count << " (" 
              << std::setprecision(2) << reversal_count / (duration/1000.0) << " Hz)" << std::endl;

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
    std::cout << "     t(s)  dist   x_pos  5-HT   DA    OA    sat   fmem  spd" << std::endl;
    int samples_per_20s = (int)(20000.0 / 100.0); // 200 samples per 20s
    for (int t = 0; t < 15; ++t) {
        int idx = (t + 1) * samples_per_20s - 1;
        if (idx < (int)sht_vs.size()) {
            // Compute satiety gain mode for this time point
            double s_sw = 1.0 / (1.0 + std::exp(-10.0 * (satiety_vs[idx] - 0.5)));
            const char* mode = (satiety_vs[idx] > 0.5) ? "<-Tc" : "->Fd";
            std::cout << "     " << std::setw(4) << (t + 1) * 20 << "  "
                      << std::setprecision(2) << std::setw(5) << dist_vs_time[idx] << "  "
                      << std::setprecision(1) << std::setw(5) << xpos_vs[idx] << "  "
                      << std::setprecision(3) << std::setw(5) << sht_vs[idx] << "  "
                      << std::setw(5) << da_vs[idx] << "  "
                      << std::setw(5) << oa_vs[idx] << "  "
                      << std::setw(5) << satiety_vs[idx] << "  "
                      << std::setw(5) << fmem_vs[idx] << "  "
                      << std::setprecision(3) << spd_scale_vs[idx]
                      << "  " << mode << std::endl;
        }
    }

    // Step 23: Thermotaxis diagnostic + gradient conflict analysis
    {
        std::cout << "\n14. THERMOTAXIS (Step 23):" << std::endl;
        Vector2d hp = sim.body().get_head_position();
        double temp_now = sim.environment().sample_temperature(hp);
        Vector2d tgrad = sim.environment().temperature_gradient(hp);
        double tgrad_mag = std::sqrt(tgrad.x * tgrad.x + tgrad.y * tgrad.y);
        int afdl_id = conn.get_neuron_id("AFDL");
        int afdr_id = conn.get_neuron_id("AFDR");
        double afdl_v = (afdl_id >= 0) ? sim.neurons()[afdl_id]->get_membrane_potential() : 0;
        double afdr_v = (afdr_id >= 0) ? sim.neurons()[afdr_id]->get_membrane_potential() : 0;
        double afd_S = 1.0 / (1.0 + std::exp(-(afdl_v - (-35.0)) / 5.0));
        std::cout << "   Temperature at head: " << std::setprecision(1) << std::fixed << temp_now << " C"
                  << "  Tc(cultivation)=" << 22.5 << " C"
                  << "  dT=" << std::setprecision(2) << (temp_now - 22.5) << std::endl;
        std::cout << "   Temp gradient: " << std::setprecision(3) << tgrad_mag << " C/mm"
                  << " (dir: " << std::setprecision(2) << tgrad.x << ", " << tgrad.y << ")"
                  << "  Tc target: x~" << std::setprecision(0) << (25.0 + (22.5 - 20.0) / 0.5) << "mm (LEFT)" << std::endl;
        std::cout << "   AFD: L=" << std::setprecision(2) << afdl_v << " mV  R=" << afdr_v << " mV"
                  << "  S(release)=" << std::setprecision(3) << afd_S << std::defaultfloat << std::endl;
        // Compare AFD vs ASE signal strength
        int asel_id2 = conn.get_neuron_id("ASEL");
        double asel_v2 = (asel_id2 >= 0) ? sim.neurons()[asel_id2]->get_membrane_potential() : 0;
        double ase_S = 1.0 / (1.0 + std::exp(-(asel_v2 - (-35.0)) / 5.0));
        std::cout << "   Signal strength: AFD_S=" << std::setprecision(3) << afd_S
                  << " vs ASE_S=" << ase_S
                  << " (ratio=" << std::setprecision(2) << (ase_S > 0.01 ? afd_S / ase_S : 0) << ")" << std::endl;
        // Satiety gain modulation (Step 23c — sigmoid switch)
        double sat = sim.satiety();
        double sw = 1.0 / (1.0 + std::exp(-10.0 * (sat - 0.5)));
        double chemo_g = 1.0 - 0.85 * sw;
        double thermo_g = 0.2 + 1.8 * sw;
        std::cout << "   Satiety gain: sat=" << std::setprecision(2) << sat
                  << " → chemo×" << std::setprecision(2) << chemo_g
                  << " thermo×" << std::setprecision(2) << thermo_g
                  << (sat > 0.5 ? " [TEMP mode]" : " [FOOD mode]") << std::endl;
        // Step 24: Pharyngeal pumping diagnostics
        std::cout << "   Pharynx: pump_rate=" << std::setprecision(1) << sim.pump_rate_hz() << " Hz"
                  << "  total_pumps=" << sim.total_pumps()
                  << "  V_muscle=" << std::setprecision(1) << sim.pharynx_V() << " mV"
                  << "  phase=";
        switch (sim.pharynx().phase()) {
            case PharyngealPump::Phase::RESTING: std::cout << "REST"; break;
            case PharyngealPump::Phase::EXCITATION: std::cout << "E"; break;
            case PharyngealPump::Phase::PLATEAU: std::cout << "P"; break;
            case PharyngealPump::Phase::REPOLARIZATION: std::cout << "R"; break;
        }
        std::cout << std::endl;
        // MC/M3 neuron voltages
        int mcl_id = conn.get_neuron_id("MCL");
        int m3l_id = conn.get_neuron_id("M3L");
        int m4_id = conn.get_neuron_id("M4");
        double mcl_v = (mcl_id >= 0) ? sim.neurons()[mcl_id]->get_membrane_potential() : 0;
        double m3l_v = (m3l_id >= 0) ? sim.neurons()[m3l_id]->get_membrane_potential() : 0;
        double m4_v = (m4_id >= 0) ? sim.neurons()[m4_id]->get_membrane_potential() : 0;
        std::cout << "   MC=" << std::setprecision(1) << mcl_v << "mV"
                  << "  M3=" << m3l_v << "mV"
                  << "  M4=" << m4_v << "mV"
                  << "  5-HT→MC: +" << std::setprecision(1) << 15.0 * sim.neuromodulation().get_concentration("5-HT") << "pA"
                  << "  OA→MC: " << std::setprecision(1) << -10.0 * sim.neuromodulation().get_concentration("OA") << "pA"
                  << std::endl;
        // Conflict analysis: did worm go toward food (right) or Tc (left)?
        // Tc target: T(x)=20+(-0.5)(x-25)=Tc → x=25-(Tc-20)/0.5
        double tc_x = 25.0 - (22.5 - 20.0) / 0.5;  // = 20mm
        double dx = hp.x - 25.0;  // positive = went RIGHT (food), negative = went LEFT (Tc)
        std::cout << "   X displacement: " << std::setprecision(1) << std::fixed << dx << " mm"
                  << (dx > 1.0 ? " -> FOOD wins" : (dx < -1.0 ? " -> TEMP wins" : " -> undecided"))
                  << "  (food@right x=35, Tc@left x=" << std::setprecision(0) << tc_x << ")"
                  << std::defaultfloat << std::endl;
    }

    // Step 25: ASH nociception diagnostic
    std::cout << "\n16. NOCICEPTION (Step 25):" << std::endl;
    std::cout << "   Repellent source: (" << repellent.x << ", " << repellent.y << ")" << std::endl;
    {
        auto head = sim.body().get_head_position();
        double rep_conc = sim.environment().sample_repellent(head);
        double rep_dx = repellent.x - head.x, rep_dy = repellent.y - head.y;
        double rep_dist = std::sqrt(rep_dx*rep_dx + rep_dy*rep_dy);
        const auto& ns = sim.neurons();
        int nn = (int)ns.size();
        std::cout << "   Repellent at head: " << std::setprecision(4) << rep_conc
                  << "  dist_to_repellent=" << std::setprecision(1) << rep_dist << " mm" << std::endl;
        if (ashl_id >= 0 && ashl_id < nn) {
            std::cout << "   ASHL: " << std::setprecision(1)
                      << ns[ashl_id]->get_membrane_potential() << " mV"
                      << "  I_ext=" << std::setprecision(2) << ns[ashl_id]->get_I_ext() << " pA" << std::endl;
        }
        if (aibl_id >= 0 && aibl_id < nn) {
            std::cout << "   AIBL: " << std::setprecision(1)
                      << ns[aibl_id]->get_membrane_potential() << " mV"
                      << "  I_syn=" << std::setprecision(2) << ns[aibl_id]->get_I_syn() << " pA" << std::endl;
        }
    }

    // Step 21: Short-term plasticity diagnostic
    std::cout << "\n15. SYNAPTIC PLASTICITY (Step 21):" << std::endl;
    const auto& synapses = sim.connectome().synapses();
    const auto& ninfos = sim.connectome().neuron_infos();
    double avg_vesicle = 0, min_vesicle = 1.0;
    int syn_count = 0;
    for (const auto& syn : synapses) {
        avg_vesicle += syn.vesicle_pool();
        if (syn.vesicle_pool() < min_vesicle) min_vesicle = syn.vesicle_pool();
        syn_count++;
    }
    if (syn_count > 0) avg_vesicle /= syn_count;
    std::cout << "   vesicle_pool: mean=" << std::setprecision(3) << avg_vesicle
              << "  min=" << min_vesicle << "  (1.0=full, 0.01=depleted)" << std::endl;
    // Show representative synapses by circuit
    for (const auto& syn : synapses) {
        int pre = syn.pre_id(), post = syn.post_id();
        if (pre < 0 || post < 0 || pre >= (int)ninfos.size() || post >= (int)ninfos.size()) continue;
        const auto& pn = ninfos[pre].name;
        const auto& qn = ninfos[post].name;
        // Show one from each circuit type
        if ((pn == "SMDDL" && qn == "SMDVL") ||
            (pn == "ALML" && (qn == "AVDL" || qn == "AVAL")) ||
            (pn == "ASER" && (qn == "AIAR" || qn == "AIYR"))) {
            std::cout << "   " << pn << "->" << qn
                      << ": n=" << std::setprecision(3) << syn.vesicle_pool()
                      << " p=" << syn.release_prob()
                      << " w_mod=" << syn.weight_mod() << std::endl;
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
