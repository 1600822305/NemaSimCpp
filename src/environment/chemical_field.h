#pragma once

#include "core/types.h"
#include <vector>
#include <cmath>

namespace celegans {

class ChemicalField {
public:
    struct Source {
        Vector2d pos;
        double strength;
    };

    ChemicalField() = default;

    void initialize(double width, double height, int grid_nx = 100, int grid_ny = 100);

    // Add a point source of attractant (sigma2: Gaussian spread in mm²)
    void add_point_source(Vector2d pos, double strength, double sigma2 = 144.0);

    // Sample concentration at a position
    double sample(Vector2d pos) const;

    // Sample spatial gradient at a position (central difference)
    Vector2d gradient(Vector2d pos) const;

    // Access point sources (for food density calculation with different σ²)
    const std::vector<Source>& sources() const { return sources_; }

    // Diffusion step (simple explicit finite difference)
    void step(double dt);

private:
    double width_ = 50.0;
    double height_ = 50.0;
    int nx_ = 100;
    int ny_ = 100;
    double dx_ = 0.5;
    double dy_ = 0.5;
    double diffusion_coeff_ = 0.001; // mm²/s

    std::vector<double> concentration_;

    std::vector<Source> sources_;

    int idx(int ix, int iy) const { return iy * nx_ + ix; }
};

} // namespace celegans
