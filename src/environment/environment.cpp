#include <cmath>
#include "core/fast_math.h"
#include "environment/environment.h"

namespace celegans {

Environment::Environment() {}

void Environment::initialize(double width, double height) {
    width_ = width;
    height_ = height;
    chem_field_.initialize(width, height, 100, 100);
    soluble_field_.initialize(width, height, 100, 100);  // Step 26b: salt/amino acids
    repellent_field_.initialize(width, height, 100, 100);
    pheromone_field_.initialize(width, height, 100, 100);  // Step 64: ascaroside pheromones
}

void Environment::step(double dt) {
    chem_field_.step(dt);
    soluble_field_.step(dt);
    repellent_field_.step(dt);
    if (has_pheromone_) pheromone_field_.step(dt);
}

double Environment::sample_chemical(Vector2d pos) const {
    return chem_field_.sample(pos);
}

double Environment::sample_soluble(Vector2d pos) const {
    return soluble_field_.sample(pos);
}

double Environment::sample_repellent(Vector2d pos) const {
    return repellent_field_.sample(pos);
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
    // Food (bacteria) is localized near source, much narrower than volatile gradient
    // σ=4mm → lawn edge (density=0.4) at ~5.4mm from center
    // vs navigation gradient σ²=144mm² (volatile attractant diffusion, σ=12mm)
    // REF: Bargmann 2006 — bacteria lawn radius on standard NGM plate
    double food = 0.0;
    const double food_sigma2 = 16.0;  // mm² (σ=4mm, bacterial lawn ~4mm radius)
    for (const auto& src : chem_field_.sources()) {
        double r2 = (pos.x - src.pos.x) * (pos.x - src.pos.x) +
                     (pos.y - src.pos.y) * (pos.y - src.pos.y);
        food += src.strength * fast_exp(-r2 / (2.0 * food_sigma2));
    }
    return food;
}

double Environment::sample_light(Vector2d pos) const {
    if (light_intensity_ <= 0.0) return 0.0;
    // Gaussian light field: σ=8mm (broader than food lawn)
    // UV/blue light scatters broadly on agar surface
    // REF: Ward 2008 — focused light on head triggers reversal
    const double light_sigma2 = 64.0;  // mm² (σ=8mm)
    double r2 = (pos.x - light_pos_.x) * (pos.x - light_pos_.x) +
                (pos.y - light_pos_.y) * (pos.y - light_pos_.y);
    return light_intensity_ * fast_exp(-r2 / (2.0 * light_sigma2));
}

void Environment::set_light_source(Vector2d pos, double intensity) {
    light_pos_ = pos;
    light_intensity_ = intensity;
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

// Step 64: Pheromone field — ascaroside social signals
// ascr#3/C9 diffuses from other worms, detected by ADL (Jang 2012)
// Modeled as static Gaussian source (simulating nearby conspecific)
double Environment::sample_pheromone(Vector2d pos) const {
    if (!has_pheromone_) return 0.0;
    return pheromone_field_.sample(pos);
}

void Environment::set_pheromone_source(Vector2d pos, double intensity) {
    has_pheromone_ = true;
    // Pheromone diffuses broadly (σ=6mm, intermediate between food lawn and volatile)
    // REF: Srinivasan 2008 — ascarosides are water-soluble, moderate diffusion
    pheromone_field_.add_point_source(pos, intensity, 36.0); // σ²=36mm²
}

// Step 118: Osmotic barrier — glycerol ring assay (Hilliard 2005)
// Ring-shaped high osmolarity barrier: sharp Gaussian profile
// Osmolarity = strength × exp(-d²/(2σ²)) where d = |r - radius|
// Worms inside ring are trapped; ASH/OSM-9 detects barrier on contact
double Environment::sample_osmolarity(Vector2d pos) const {
    if (osm_strength_ <= 0.0) return 0.0;
    double dx = pos.x - osm_center_.x;
    double dy = pos.y - osm_center_.y;
    double r = std::sqrt(dx * dx + dy * dy);
    double d = r - osm_radius_;  // distance from ring center
    double sigma2 = osm_width_ * osm_width_ * 0.5;  // half-width squared
    return osm_strength_ * fast_exp(-d * d / (2.0 * sigma2));
}

void Environment::set_osmotic_barrier(Vector2d center, double radius, double width, double strength) {
    osm_center_ = center;
    osm_radius_ = radius;
    osm_width_ = width;
    osm_strength_ = strength;
}

} // namespace celegans
