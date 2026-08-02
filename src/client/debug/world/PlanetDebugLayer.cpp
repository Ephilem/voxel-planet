//
// Created by raph on 04/08/2026.
//

#include "PlanetDebugLayer.h"

#include <iterator>

using namespace vp;

static void sample_heights(const PlanetTerrainSampler &sampler,
                           const PlanetDirectionField &field,
                           int lod, float *out) {
    sampler.sample_height_batch(field.x.data(), field.y.data(), field.z.data(),
                                field.count(), lod, out, thread_scratch());
}

PlanetLayerDebug vp::make_elevation_layer() {
    PlanetLayerDebug layer;
    layer.name = "Elevation";
    layer.unit = "m";
    layer.divergingAroundZero = true; // keep the neutral color on sea level
    layer.colormap = ImPlotColormap_Spectral;
    layer.invertColormap = true;
    layer.evaluate = sample_heights;
    return layer;
}

PlanetLayerDebug vp::make_landmask_layer() {
    PlanetLayerDebug layer;
    layer.name = "Land / sea";
    layer.unit = "";
    layer.autoRange = false;
    layer.rangeMin = 0.0f;
    layer.rangeMax = 1.0f;
    layer.colormap = ImPlotColormap_Greys;
    layer.interpolable = false;

    layer.evaluate = [](const PlanetTerrainSampler &sampler,
                        const PlanetDirectionField &field,
                        int lod, float *out) {
        sample_heights(sampler, field, lod, out);

        const int n = field.count();
        for (int i = 0; i < n; ++i)
            out[i] = out[i] > 0.0f ? 1.0f : 0.0f;
    };
    return layer;
}

using LayerFactory = PlanetLayerDebug (*)();

static const LayerFactory LAYER_FACTORIES[] = {
    &make_elevation_layer,
    &make_landmask_layer
};

static const char *LAYER_NAMES[] = {
    "Elevation",
    "Land / sea"
};

int vp::layer_count() {
    return static_cast<int>(std::size(LAYER_FACTORIES));
}

const char *vp::layer_name_at(int index) {
    if (index < 0 || index >= layer_count())
        return "";
    return LAYER_NAMES[index];
}

PlanetLayerDebug vp::layer_at(int index) {
    if (index < 0 || index >= layer_count())
        return {};
    return LAYER_FACTORIES[index]();
}

