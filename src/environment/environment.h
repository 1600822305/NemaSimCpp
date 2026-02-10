#pragma once

#include "core/types.h"
#include "environment/chemical_field.h"

namespace celegans {

class Environment {
public:
    Environment();

    void initialize(double width, double height);
    void step(double dt);

    double sample_chemical(Vector2d pos) const;
    double sample_temperature(Vector2d pos) const;
    Vector2d temperature_gradient(Vector2d pos) const;

    double width() const { return width_; }
    double height() const { return height_; }

    ChemicalField& chemical_field() { return chem_field_; }
    const ChemicalField& chemical_field() const { return chem_field_; }

    // Temperature field configuration (Step 23)
    void set_temperature_gradient(double center_temp, Vector2d gradient_dir, double gradient_strength);
    double center_temperature() const { return temp_center_; }

private:
    double width_ = 50.0;   // mm
    double height_ = 50.0;  // mm
    ChemicalField chem_field_;

    // Temperature field: linear gradient (Step 23 — Mori 1995)
    // T(x,y) = temp_center_ + grad_x_ * (x - cx) + grad_y_ * (y - cy)
    double temp_center_ = 20.0;  // °C at arena center
    double temp_grad_x_ = 0.0;   // °C/mm in x direction
    double temp_grad_y_ = 0.0;   // °C/mm in y direction
};

} // namespace celegans
