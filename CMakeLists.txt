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

# Simulation engine
add_library(celegans_simulation STATIC
    src/simulation/simulation_engine.cpp
    src/simulation/setup_neuromodulation.cpp
    src/simulation/update_internal_states.cpp
    src/simulation/update_learning.cpp
    src/simulation/update_pharynx_system.cpp
)
target_link_libraries(celegans_simulation PUBLIC
    celegans_connectome
    celegans_body
    celegans_motor
    celegans_environment
    celegans_neuromodulation
    celegans_compute
)

# Main executable (headless)
add_executable(celegans_sim src/simulation/main.cpp)
target_link_libraries(celegans_sim PRIVATE celegans_simulation)

# Diagnostic executable (signal chain analysis)
add_executable(celegans_diag src/simulation/diag_main.cpp)
target_link_libraries(celegans_diag PRIVATE celegans_simulation)
if(CELEGANS_USE_OPENMP)
    target_link_libraries(celegans_diag PRIVATE OpenMP::OpenMP_CXX)
endif()

# Regression test executable (automated bug detection + current tracing)
add_executable(celegans_regtest src/simulation/regression_test.cpp)
target_link_libraries(celegans_regtest PRIVATE celegans_simulation)

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
