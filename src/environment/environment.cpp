#include "environment/environment.h"

namespace celegans {

Environment::Environment() {}

void Environment::initialize(double width, double height) {
    width_ = width;
    height_ = height;
    chem_field_.initialize(width, height, 100, 100);
}

void Environment::step(double dt) {
    chem_field_.step(dt);
}

double Environment::sample_chemical(Vector2d pos) const {
    return chem_field_.sample(pos);
}

double Environment::sample_temperature(Vector2d pos) const {
    double cx = width_ / 2.0;
    double cy = height_ / 2.0;
    return temp_center_ + temp_grad_x_ * (pos.x - cx) + temp_grad_y_ * (pos.y - cy);
}

Vector2d Environment::temperature_gradient(Vector2d pos) const {
    (void)pos; // linear gradient is constant everywhere
    return {temp_grad_x_, temp_grad_y_};
}

double Environment::sample_food_density(Vector2d pos) const {
    // Food (bacteria) is localized: σ²=9mm² (σ≈3mm radius colony)
    // vs navigation gradient σ²=144mm² (volatile attractant diffusion)
    // REF: Bargmann 2006 — bacteria lawn ~3mm diameter on agar
    double food = 0.0;
    const double food_sigma2 = 16.0;  // mm² (σ=4mm, bacterial lawn ~4mm radius)
    for (const auto& src : chem_field_.sources()) {
        double r2 = (pos.x - src.pos.x) * (pos.x - src.pos.x) +
                     (pos.y - src.pos.y) * (pos.y - src.pos.y);
        food += src.strength * std::exp(-r2 / (2.0 * food_sigma2));
    }
    return food;
}

void Environment::set_temperature_gradient(double center_temp, Vector2d gradient_dir, double gradient_strength) {
    temp_center_ = center_temp;
    double len = std::sqrt(gradient_dir.x * gradient_dir.x + gradient_dir.y * gradient_dir.y);
    if (len > 1e-9) {
        temp_grad_x_ = gradient_dir.x / len * gradient_strength;
        temp_grad_y_ = gradient_dir.y / len * gradient_strength;
    } else {
        temp_grad_x_ = 0.0;
        temp_grad_y_ = 0.0;
    }
}

} // namespace celegans
