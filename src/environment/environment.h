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

    // Step 55: Light field — UV/blue light source (Ward 2008 Nat Neurosci)
    // C. elegans detects light via LITE-1 in ASJ/ASK/AWB/ASH neurons
    double sample_light(Vector2d pos) const;
    void set_light_source(Vector2d pos, double intensity);
    bool has_light() const { return light_intensity_ > 0.0; }

    double width() const { return width_; }
    double height() const { return height_; }

    ChemicalField& chemical_field() { return chem_field_; }          // food odor (volatile)
    const ChemicalField& chemical_field() const { return chem_field_; }
    ChemicalField& soluble_field() { return soluble_field_; }          // salt/amino acids
    const ChemicalField& soluble_field() const { return soluble_field_; }
    ChemicalField& repellent_field() { return repellent_field_; }
    const ChemicalField& repellent_field() const { return repellent_field_; }

    // Step 64: Pheromone field — ascaroside social signals (Srinivasan 2008)
    // ascr#3/C9: hermaphrodite avoidance pheromone detected by ADL
    ChemicalField& pheromone_field() { return pheromone_field_; }
    const ChemicalField& pheromone_field() const { return pheromone_field_; }
    double sample_pheromone(Vector2d pos) const;
    bool has_pheromone() const { return has_pheromone_; }
    void set_pheromone_source(Vector2d pos, double intensity);

    // Step 118: Osmotic barrier field (glycerol ring assay, Hilliard 2005)
    // High osmolarity ring/barrier that worms avoid via ASH OSM-9/TRPV
    double sample_osmolarity(Vector2d pos) const;
    void set_osmotic_barrier(Vector2d center, double radius, double width, double strength);
    bool has_osmotic_barrier() const { return osm_strength_ > 0.0; }

    // Temperature field configuration (Step 23)
    void set_temperature_gradient(double center_temp, Vector2d gradient_dir, double gradient_strength);
    double center_temperature() const { return temp_center_; }

private:
    double width_ = 50.0;   // mm
    double height_ = 50.0;  // mm
    ChemicalField chem_field_;        // food odor (volatile, bacteria-specific) → AWC/AWA
    ChemicalField soluble_field_;      // Step 26b: salt/amino acids (water-soluble) → ASE
    ChemicalField repellent_field_;    // Step 25: noxious chemicals → ASH
    ChemicalField pheromone_field_;    // Step 64: ascaroside pheromones → ADL
    bool has_pheromone_ = false;        // flag for pheromone presence

    // Temperature field: linear gradient (Step 23 — Mori 1995)
    // T(x,y) = temp_center_ + grad_x_ * (x - cx) + grad_y_ * (y - cy)
    double temp_center_ = 20.0;  // °C at arena center
    double temp_grad_x_ = 0.0;   // °C/mm in x direction
    double temp_grad_y_ = 0.0;   // °C/mm in y direction

    // Step 118: Osmotic barrier (glycerol ring)
    Vector2d osm_center_ = {25.0, 25.0}; // center of ring
    double osm_radius_ = 15.0;           // mm, ring radius
    double osm_width_ = 1.5;             // mm, ring width (sharp barrier)
    double osm_strength_ = 0.0;          // [0,1] osmolarity intensity (0=off)

    // Step 55: Light source (UV/blue)
    Vector2d light_pos_ = {-1.0, -1.0}; // negative = no light
    double light_intensity_ = 0.0;       // [0,1] normalized intensity
};

} // namespace celegans
