cmake_minimum_required(VERSION 3.16)
project(CElegans VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(CELEGANS_BUILD_TESTS "Build tests" ON)
option(CELEGANS_USE_OPENMP "Enable OpenMP parallelization" ON)
option(CELEGANS_USE_OPENCL "Enable OpenCL GPU acceleration" ON)
option(CELEGANS_BUILD_VIS "Build real-time visualization (requires SDL2+ImGui+ImPlot)" ON)

if(CELEGANS_USE_OPENMP)
    find_package(OpenMP REQUIRED)
endif()

if(CELEGANS_USE_OPENCL)
    find_package(OpenCL CONFIG QUIET)
    if(NOT OpenCL_FOUND)
        find_package(OpenCL QUIET)
    endif()
    if(OpenCL_FOUND)
        message(STATUS "OpenCL found: ${OpenCL_VERSION_STRING}")
    else()
        message(STATUS "OpenCL not found — GPU acceleration disabled")
        set(CELEGANS_USE_OPENCL OFF)
    endif()
endif()

# Core library
add_library(celegans_core STATIC
    src/core/types.cpp
    src/core/config.cpp
    src/core/logger.cpp
)
target_include_directories(celegans_core PUBLIC ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(celegans_core PUBLIC /utf-8)
endif()

# Neuron library
add_library(celegans_neuron STATIC
    src/neuron/ion_channel.cpp
    src/neuron/calcium_dynamics.cpp
    src/neuron/single_compartment.cpp
    src/neuron/multi_compartment.cpp
    src/neuron/neuron_factory.cpp
)
target_link_libraries(celegans_neuron PUBLIC celegans_core)

# Connectome library
add_library(celegans_connectome STATIC
    src/connectome/chemical_synapse.cpp
    src/connectome/gap_junction.cpp
    src/connectome/connectome.cpp
    src/connectome/connectome_loader.cpp
    src/connectome/connectome_builder.cpp
)
target_link_libraries(celegans_connectome PUBLIC celegans_neuron)

# Body library
add_library(celegans_body STATIC
    src/body/body_model.cpp
    src/body/muscle_system.cpp
)
target_link_libraries(celegans_body PUBLIC celegans_core)

# Motor library
add_library(celegans_motor STATIC
    src/motor/motor_controller.cpp
)
target_link_libraries(celegans_motor PUBLIC celegans_neuron celegans_body)

# Environment library
add_library(celegans_environment STATIC
    src/environment/environment.cpp
    src/environment/chemical_field.cpp
)
target_link_libraries(celegans_environment PUBLIC celegans_core)

# Neuromodulation library (Layer 6)
add_library(celegans_neuromodulation STATIC
    src/neuromodulation/neuromodulation.cpp
)
target_link_libraries(celegans_neuromodulation PUBLIC celegans_neuron celegans_connectome)

# Compute backend library (CPU + optional OpenCL GPU)
add_library(celegans_compute STATIC
    src/compute/opencl_backend.cpp
)
target_link_libraries(celegans_compute PUBLIC celegans_core)
if(CELEGANS_USE_OPENCL)
    target_link_libraries(celegans_compute PUBLIC OpenCL::OpenCL)
    target_compile_definitions(celegans_compute PUBLIC CELEGANS_HAS_OPENCL=1)
endif()

# Diagnostics library (modular analysis tools)
add_library(celegans_diagnostics STATIC
    src/diagnostics/diagnostic_tracker.cpp
    src/diagnostics/output_formatter.cpp
)
target_link_libraries(celegans_diagnostics PUBLIC celegans_core celegans_neuron celegans_connectome)

# Simulation engine
add_library(celegans_simulation STATIC
    src/simulation/simulation_engine.cpp
    src/simulation/setup_neuromodulation.cpp
    src/simulation/update_internal_states.cpp
    src/simulation/update_learning.cpp
    src/simulation/update_pharynx_system.cpp
    src/simulation/apply_sensory_systems.cpp
    src/simulation/apply_motor_control.cpp
    src/simulation/setup_gpu_stp.cpp
)
target_link_libraries(celegans_simulation PUBLIC
    celegans_connectome
    celegans_body
    celegans_motor
    celegans_environment
    celegans_neuromodulation
    celegans_compute
)

# Diagnostic executable (signal chain analysis) - ARCHIVED
# add_executable(celegans_diag src/simulation/diag_main.cpp)
# target_link_libraries(celegans_diag PRIVATE celegans_simulation)
# if(CELEGANS_USE_OPENMP)
#     target_link_libraries(celegans_diag PRIVATE OpenMP::OpenMP_CXX)
# endif()

# Neuron Monitor executable (neural activity monitoring)
add_executable(neuron_monitor src/diagnostics/neuron_monitor_main.cpp)
target_link_libraries(neuron_monitor PRIVATE celegans_simulation celegans_diagnostics)

# Health Check executable (quick neuron health scan)
add_executable(health_check src/diagnostics/health_check_main.cpp)
target_link_libraries(health_check PRIVATE celegans_simulation)

# Circuit Probe executable (neural circuit signal tracing)
add_executable(circuit_probe src/diagnostics/circuit_probe_main.cpp)
target_link_libraries(circuit_probe PRIVATE celegans_simulation)

# Behavior Analyzer executable (research-grade behavior analysis)
add_executable(behavior_analyzer src/diagnostics/behavior_analyzer_main.cpp)
target_link_libraries(behavior_analyzer PRIVATE celegans_simulation)

# Event Logger executable (discrete event recording)
add_executable(event_logger src/diagnostics/event_logger_main.cpp)
target_link_libraries(event_logger PRIVATE celegans_simulation)

# Causal Analyzer executable (TE, GC, intervention causality)
add_executable(causal_analyzer src/diagnostics/causal_analyzer_main.cpp)
target_link_libraries(causal_analyzer PRIVATE celegans_simulation)

# Emergence Detector executable (Phi, MSE, metastability)
add_executable(emergence_detector src/diagnostics/emergence_detector_main.cpp)
target_link_libraries(emergence_detector PRIVATE celegans_simulation)

# Performance Profiler executable (timing, memory, bottleneck analysis)
add_executable(perf_profiler src/diagnostics/perf_profiler_main.cpp)
target_link_libraries(perf_profiler PRIVATE celegans_simulation)

# Parameter Sweep executable (automated parameter space search)
add_executable(param_sweep src/diagnostics/param_sweep_main.cpp)
target_link_libraries(param_sweep PRIVATE celegans_simulation)

# Wave Analyzer executable (traveling wave & RFT locomotion diagnostics)
add_executable(wave_analyzer src/diagnostics/wave_analyzer_main.cpp)
target_link_libraries(wave_analyzer PRIVATE celegans_simulation)

# Visualization executable (Dear ImGui + ImPlot + SDL2 + OpenGL)
if(CELEGANS_BUILD_VIS)
    find_package(imgui CONFIG REQUIRED)
    find_package(implot CONFIG REQUIRED)
    find_package(glfw3 CONFIG REQUIRED)
    find_package(OpenGL REQUIRED)

    add_executable(celegans_vis
        src/visualization/vis_main.cpp
        src/visualization/vis_app.cpp
    )
    target_link_libraries(celegans_vis PRIVATE
        celegans_simulation
        imgui::imgui
        implot::implot
        glfw
        OpenGL::GL
    )
    target_include_directories(celegans_vis PRIVATE ${CMAKE_SOURCE_DIR}/src)
    # Disable char8_t (C++20) so u8"" stays const char* for ImGui compatibility
    if(MSVC)
        target_compile_options(celegans_vis PRIVATE /Zc:char8_t- /utf-8)
    endif()
endif()

if(CELEGANS_USE_OPENMP)
    target_link_libraries(celegans_core PUBLIC OpenMP::OpenMP_CXX)
endif()
