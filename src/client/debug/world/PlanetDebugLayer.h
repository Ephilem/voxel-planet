#pragma once

#include <functional>

#include <implot.h>

#include "PlanetDirectionField.h"
#include "core/world/planet/generator/PlanetTerrainSampler.h"

namespace vp {
    /**
     * What to measure on the sphere, and how to read the result
     */
    struct PlanetLayerDebug {
        const char *name = "";
        const char *unit = "";

        /**
         * Fills outValues[field.count()] from a tile of directions
         */
        std::function<void(const PlanetTerrainSampler &sampler,
                           const PlanetDirectionField &field,
                           int lod,
                           float *outValues)> evaluate;

        float rangeMin = 0.0f;
        float rangeMax = 0.0f;
        bool autoRange = true;

        /**
         * Forces the scale symmetric around zero
         */
        bool divergingAroundZero = false;

        ImPlotColormap colormap = ImPlotColormap_Spectral;
        bool interpolable = true;

        bool invertColormap = false;
    };

    PlanetLayerDebug make_elevation_layer();
    PlanetLayerDebug make_landmask_layer();

    int layer_count();
    /// Display name only, without building the layer.
    const char *layer_name_at(int index);
    PlanetLayerDebug layer_at(int index);
}
