#include "environment/chemical_field.h"
#include <algorithm>

namespace celegans {

void ChemicalField::initialize(double width, double height, int grid_nx, int grid_ny) {
    width_ = width;
    height_ = height;
    nx_ = grid_nx;
    ny_ = grid_ny;
    dx_ = width_ / nx_;
    dy_ = height_ / ny_;
    concentration_.assign(nx_ * ny_, 0.0);
}

void ChemicalField::add_point_source(Vector2d pos, double strength) {
    sources_.push_back({pos, strength});
    // Initialize Gaussian around source
    for (int iy = 0; iy < ny_; ++iy) {
        for (int ix = 0; ix < nx_; ++ix) {
            double cx = (ix + 0.5) * dx_;
            double cy = (iy + 0.5) * dy_;
            double r2 = (cx - pos.x) * (cx - pos.x) + (cy - pos.y) * (cy - pos.y);
            double sigma2 = 25.0; // mm²
            concentration_[idx(ix, iy)] += strength * std::exp(-r2 / (2.0 * sigma2));
        }
    }
}

double ChemicalField::sample(Vector2d pos) const {
    if (concentration_.empty()) return 0.0;
    // Bilinear interpolation
    double fx = pos.x / dx_ - 0.5;
    double fy = pos.y / dy_ - 0.5;
    int ix = static_cast<int>(fx);
    int iy = static_cast<int>(fy);
    ix = std::clamp(ix, 0, nx_ - 2);
    iy = std::clamp(iy, 0, ny_ - 2);
    double tx = fx - ix;
    double ty = fy - iy;
    tx = std::clamp(tx, 0.0, 1.0);
    ty = std::clamp(ty, 0.0, 1.0);

    double c00 = concentration_[idx(ix, iy)];
    double c10 = concentration_[idx(ix + 1, iy)];
    double c01 = concentration_[idx(ix, iy + 1)];
    double c11 = concentration_[idx(ix + 1, iy + 1)];

    return c00 * (1 - tx) * (1 - ty) + c10 * tx * (1 - ty) +
           c01 * (1 - tx) * ty + c11 * tx * ty;
}

void ChemicalField::step(double dt) {
    if (concentration_.empty()) return;
    // Simple explicit diffusion (for MVP, not performance-critical)
    std::vector<double> next = concentration_;
    double rx = diffusion_coeff_ * dt / (dx_ * dx_);
    double ry = diffusion_coeff_ * dt / (dy_ * dy_);

    // Stability check
    if (rx + ry > 0.5) return; // skip if timestep too large for stability

    for (int iy = 1; iy < ny_ - 1; ++iy) {
        for (int ix = 1; ix < nx_ - 1; ++ix) {
            int i = idx(ix, iy);
            next[i] = concentration_[i] +
                rx * (concentration_[idx(ix+1, iy)] - 2.0*concentration_[i] + concentration_[idx(ix-1, iy)]) +
                ry * (concentration_[idx(ix, iy+1)] - 2.0*concentration_[i] + concentration_[idx(ix, iy-1)]);
        }
    }
    concentration_ = next;
}

} // namespace celegans
