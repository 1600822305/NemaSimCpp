cmake_minimum_required(VERSION 3.16)
project(CElegans VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Options
option(CELEGANS_BUILD_TESTS "Build tests" ON)
option(CELEGANS_USE_OPENMP "Enable OpenMP parallelization" OFF)
option(CELEGANS_BUILD_VIS "Build real-time visualization (requires SDL2+ImGui+ImPlot)" ON)

if(CELEGANS_USE_OPENMP)
    find_package(OpenMP REQUIRED)
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

# Simulation engine
add_library(celegans_simulation STATIC
    src/simulation/simulation_engine.cpp
)
target_link_libraries(celegans_simulation PUBLIC
    celegans_connectome
    celegans_body
    celegans_motor
    celegans_environment
)

# Main executable (headless)
add_executable(celegans_sim src/simulation/main.cpp)
target_link_libraries(celegans_sim PRIVATE celegans_simulation)

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
endif()

if(CELEGANS_USE_OPENMP)
    target_link_libraries(celegans_core PUBLIC OpenMP::OpenMP_CXX)
endif()
