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

    double width() const { return width_; }
    double height() const { return height_; }

    ChemicalField& chemical_field() { return chem_field_; }
    const ChemicalField& chemical_field() const { return chem_field_; }

private:
    double width_ = 50.0;   // mm
    double height_ = 50.0;  // mm
    double temperature_ = 20.0; // °C (uniform for now)
    ChemicalField chem_field_;
};

} // namespace celegans
