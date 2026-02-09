#include "simulation/simulation_engine.h"
#include "core/logger.h"
#include <iostream>
#include <iomanip>
#include <fstream>

using namespace celegans;

int main(int argc, char* argv[]) {
    Logger::instance().set_level(LogLevel::INFO);

    SimulationEngine sim;

    if (argc > 1) {
        Config config;
        config.load_from_file(argv[1]);
        sim.initialize(config);
    } else {
        sim.initialize_default();
    }

    // Open output file for trajectory and neural activity
    std::ofstream traj_file("trajectory.csv");
    traj_file << "time_ms,head_x,head_y,head_angle,speed";

    // Add headers for a subset of key neurons
    std::vector<std::string> monitor_neurons = {
        "ASEL", "ASER", "AVAL", "AVAR", "AVBL", "AVBR",
        "DB01", "VB01", "DA01", "VA01", "SMDDL", "SMDVL"
    };
    std::vector<int> monitor_ids;
    for (auto& name : monitor_neurons) {
        int id = sim.connectome().get_neuron_id(name);
        if (id >= 0) {
            monitor_ids.push_back(id);
            traj_file << ",V_" << name;
        }
    }
    traj_file << "\n";

    // Set up step callback for periodic logging and recording
    int report_interval = 1000; // every 1000 steps
    int record_interval = 10;   // record every 10 steps

    sim.set_step_callback([&](const SimulationEngine& engine, int step_num) {
        // Record trajectory
        if (step_num % record_interval == 0) {
            auto head = engine.body().get_head_position();
            traj_file << std::fixed << std::setprecision(3)
                      << engine.current_time() << ","
                      << head.x << "," << head.y << ","
                      << engine.body().get_head_angle() << ","
                      << engine.body().get_speed();

            for (int id : monitor_ids) {
                traj_file << "," << std::setprecision(2)
                          << engine.neurons()[id]->get_membrane_potential();
            }
            traj_file << "\n";
        }

        // Periodic console report
        if (step_num % report_interval == 0 && step_num > 0) {
            auto head = engine.body().get_head_position();
            std::cout << "[t=" << std::fixed << std::setprecision(1)
                      << engine.current_time() << " ms] "
                      << "head=(" << std::setprecision(2) << head.x
                      << ", " << head.y << ") "
                      << "speed=" << std::setprecision(4) << engine.body().get_speed()
                      << " mm/s";

            // Print a few key neuron potentials
            if (monitor_ids.size() >= 12) {
                std::cout << " | Vd="
                          << std::setprecision(1)
                          << engine.neurons()[monitor_ids[10]]->get_membrane_potential()
                          << " Vv="
                          << engine.neurons()[monitor_ids[11]]->get_membrane_potential();
            }
            // Diagnostic: head muscle differential and max curvature
            double max_diff = 0.0, max_curv = 0.0;
            for (int s = 0; s < NUM_BODY_SEGMENTS; ++s) {
                auto& seg = engine.body().segments()[s];
                double diff = std::abs(seg.dorsal_activation - seg.ventral_activation);
                if (diff > max_diff) max_diff = diff;
                if (std::abs(seg.curvature) > max_curv) max_curv = std::abs(seg.curvature);
            }
            std::cout << " | mdiff=" << std::setprecision(4) << max_diff
                      << " mcurv=" << max_curv;
            std::cout << std::endl;
        }
    });

    // Run for 5 seconds (5000 ms)
    double sim_duration = 5000.0; // ms
    std::cout << "\n=== C. elegans Neural Simulation ===" << std::endl;
    std::cout << "Neurons: " << sim.connectome().num_neurons() << std::endl;
    std::cout << "Chemical synapses: " << sim.connectome().num_synapses() << std::endl;
    std::cout << "Gap junctions: " << sim.connectome().num_gap_junctions() << std::endl;
    std::cout << "Duration: " << sim_duration << " ms" << std::endl;
    std::cout << "Time step: " << sim.dt() << " ms" << std::endl;
    std::cout << "====================================\n" << std::endl;

    sim.run(sim_duration);

    traj_file.close();
    std::cout << "\nTrajectory saved to trajectory.csv" << std::endl;

    // Print final state summary
    auto head = sim.body().get_head_position();
    auto tail = sim.body().get_tail_position();
    std::cout << "\n--- Final State ---" << std::endl;
    std::cout << "Head position: (" << head.x << ", " << head.y << ")" << std::endl;
    std::cout << "Tail position: (" << tail.x << ", " << tail.y << ")" << std::endl;
    std::cout << "Distance from start: "
              << (head - Vector2d{25.0, 25.0}).norm() << " mm" << std::endl;

    return 0;
}
