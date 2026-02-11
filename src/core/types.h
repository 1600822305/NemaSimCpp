#pragma once

#include <cmath>
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <functional>
#include <unordered_map>
#include <cassert>
#include <cstdint>

namespace celegans {

struct Vector2d {
    double x = 0.0;
    double y = 0.0;

    Vector2d() = default;
    Vector2d(double x, double y) : x(x), y(y) {}

    Vector2d operator+(const Vector2d& o) const { return {x + o.x, y + o.y}; }
    Vector2d operator-(const Vector2d& o) const { return {x - o.x, y - o.y}; }
    Vector2d operator*(double s) const { return {x * s, y * s}; }
    Vector2d operator/(double s) const { return {x / s, y / s}; }
    Vector2d& operator+=(const Vector2d& o) { x += o.x; y += o.y; return *this; }
    Vector2d& operator-=(const Vector2d& o) { x -= o.x; y -= o.y; return *this; }
    Vector2d& operator*=(double s) { x *= s; y *= s; return *this; }

    double norm() const { return std::sqrt(x * x + y * y); }
    double norm_sq() const { return x * x + y * y; }
    Vector2d normalized() const {
        double n = norm();
        return (n > 1e-12) ? Vector2d{x / n, y / n} : Vector2d{0, 0};
    }
    double dot(const Vector2d& o) const { return x * o.x + y * o.y; }
    double cross(const Vector2d& o) const { return x * o.y - y * o.x; }

    static Vector2d from_angle(double angle) {
        return {std::cos(angle), std::sin(angle)};
    }
};

inline Vector2d operator*(double s, const Vector2d& v) { return v * s; }

enum class NeuronType : uint8_t {
    SENSORY,
    INTER,
    MOTOR,
    PHARYNGEAL,
    UNKNOWN
};

enum class NeurotransmitterType : uint8_t {
    ACETYLCHOLINE,   // ACh - excitatory
    GABA,            // inhibitory
    GLUTAMATE,       // excitatory/inhibitory
    DOPAMINE,        // modulatory
    SEROTONIN,       // modulatory (5-HT)
    TYRAMINE,        // modulatory
    OCTOPAMINE,      // modulatory
    UNKNOWN
};

enum class SensoryModality : uint8_t {
    CHEMOSENSORY_ATTRACTIVE,
    CHEMOSENSORY_REPULSIVE,
    MECHANOSENSORY_ANTERIOR,
    MECHANOSENSORY_POSTERIOR,
    THERMOSENSORY,
    PROPRIOCEPTIVE,
    NOCICEPTIVE,
    NONE
};

struct NeuronInfo {
    int id = -1;
    std::string name;
    NeuronType type = NeuronType::UNKNOWN;
    NeurotransmitterType neurotransmitter = NeurotransmitterType::UNKNOWN;
    SensoryModality modality = SensoryModality::NONE;
    bool is_left = false;     // left/right pair
    int pair_id = -1;         // id of left/right counterpart
};

struct SynapseInfo {
    int pre_neuron_id = -1;
    int post_neuron_id = -1;
    double num_sections = 1.0;  // EM section count (double: supports extrasynaptic <1.0)
    NeurotransmitterType neurotransmitter = NeurotransmitterType::UNKNOWN;
    int post_compartment = 0; // Step 28: target compartment (0=soma, default)
};

struct GapJunctionInfo {
    int neuron_a_id = -1;
    int neuron_b_id = -1;
    double num_sections = 1.0;  // EM section count (double: supports fractional)
    int compartment_a = 0;    // Step 28: compartment on neuron A (0=soma)
    int compartment_b = 0;    // Step 28: compartment on neuron B (0=soma)
};

using NeuronId = int;

constexpr int NUM_SOMATIC_NEURONS = 302;
constexpr int NUM_PHARYNGEAL_NEURONS = 20;
constexpr int NUM_TOTAL_NEURONS = NUM_SOMATIC_NEURONS + NUM_PHARYNGEAL_NEURONS;
constexpr int NUM_BODY_SEGMENTS = 48;
constexpr int NUM_MUSCLES = 95;

constexpr double PI = 3.14159265358979323846;

} // namespace celegans
