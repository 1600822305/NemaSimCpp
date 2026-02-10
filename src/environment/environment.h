#pragma once

#include "core/types.h"
#include "environment/chemical_field.h"

namespace celegans {

class Environment {
public:
    Environment();

    void initialize(double width, double height);
    void step(double dt);

    double sample_chemical(Vector2d pos) const;      // food odor (volatile, AWC/AWA)
    double sample_soluble(Vector2d pos) const;        // salt/amino acids (ASE)
    double sample_repellent(Vector2d pos) const;
    double sample_temperature(Vector2d pos) const;
    Vector2d temperature_gradient(Vector2d pos) const;

    // Food density: narrow Gaussian around food sources (σ²=9mm², σ≈3mm)
    // Biologically: volatile attractants diffuse widely (σ≈12mm) for navigation,
    // but bacteria (food) are localized in a small colony (~3mm radius).
    // Used for feeding/satiety, NOT for gradient-based navigation.
    double sample_food_density(Vector2d pos) const;

    double width() const { return width_; }
    double height() const { return height_; }

    ChemicalField& chemical_field() { return chem_field_; }          // food odor (volatile)
    const ChemicalField& chemical_field() const { return chem_field_; }
    ChemicalField& soluble_field() { return soluble_field_; }          // salt/amino acids
    const ChemicalField& soluble_field() const { return soluble_field_; }
    ChemicalField& repellent_field() { return repellent_field_; }
    const ChemicalField& repellent_field() const { return repellent_field_; }

    // Temperature field configuration (Step 23)
    void set_temperature_gradient(double center_temp, Vector2d gradient_dir, double gradient_strength);
    double center_temperature() const { return temp_center_; }

private:
    double width_ = 50.0;   // mm
    double height_ = 50.0;  // mm
    ChemicalField chem_field_;        // food odor (volatile, bacteria-specific) → AWC/AWA
    ChemicalField soluble_field_;      // Step 26b: salt/amino acids (water-soluble) → ASE
    ChemicalField repellent_field_;    // Step 25: noxious chemicals → ASH

    // Temperature field: linear gradient (Step 23 — Mori 1995)
    // T(x,y) = temp_center_ + grad_x_ * (x - cx) + grad_y_ * (y - cy)
    double temp_center_ = 20.0;  // °C at arena center
    double temp_grad_x_ = 0.0;   // °C/mm in x direction
    double temp_grad_y_ = 0.0;   // °C/mm in y direction
};

} // namespace celegans
