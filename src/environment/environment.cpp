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
    (void)pos;
    return temperature_;
}

} // namespace celegans
