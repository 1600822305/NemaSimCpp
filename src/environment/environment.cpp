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
