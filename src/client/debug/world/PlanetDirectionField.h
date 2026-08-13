//
// Created by raph on 04/08/2026.
//
#pragma once
#include <vector>

#include "core/world/planet/generator/PlanetTerrainSampler.h"

namespace vp {
/**
 * Unit directions of a tiles
 *
 * The split x/y/z layout is what GenPositionArray3D expects: three separate
 * arrays
 */
struct PlanetDirectionField {
    std::vector<float> x, y, z;
    int width = 0;
    int height = 0;

    int count() const { return width * height; }

    /**
     * Allocates width * height + SIMD_PADDING floats per axis.
     */
    void resize(int w, int h) {
        width = w;
        height = h;

        const size_t padded = static_cast<size_t>(count()) + PlanetTerrainSampler::SIMD_PADDING;
        x.assign(padded, 0.0f);
        y.assign(padded, 0.0f);
        z.assign(padded, 1.0f);
    }
};
} // namespace vp
