#pragma once

#include <implot.h>
#include <string>
#include <vector>

#include <glm/vec2.hpp>

#include "client/debug/IDebugPanel.h"
#include "core/world/planet/planet_components.h"
#include "PlanetDebugLayer.h"
#include "PlanetDirectionField.h"
#include "PlanetLayerProjectionMappings.h"
#include "renderer/debug/DebugImageTexture.h"

namespace vp {
class PlanetLayerViewer : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;

    const std::string name() const override { return "Planet Layers"; }

    const std::string category() const override { return "World"; }

private:
    struct PlanetRepresentation {
        flecs::entity entity;
        Planet planetComp;
        PlanetTerrainParams terrainParams;
    };

    void draw_map(flecs::world& ecs);
    void draw_controls(flecs::world& ecs);
    void draw_params();
    void draw_cursor_readout(const PlanetLayerProjectionMapping& proj, const ImPlotPoint& mouse) const;

    void select_planet(PlanetRepresentation planet);
    void apply_generation_params();
    void collect_planets(flecs::world& ecs, std::vector<PlanetRepresentation>& planets);

    /// Samples the projection using the last applied params, using m_appliedParams
    void generate_texture();
    /// m_values -> m_pixels, using the layer range and colormap
    void colorize(const PlanetLayerDebug& layer);

    /// Raw value readback. NaN outside the image or on a projection gap
    float value_at(int x, int y) const;
    /// Plot coordinates -> pixel. False when outside the image
    bool plot_to_pixel(const PlanetLayerProjectionMapping& proj, const ImPlotPoint& mouse, int& outX, int& outY) const;

    PlanetRepresentation m_selectedPlanet = {};

    // query cache
    flecs::query<const Planet, const PlanetTerrainParams> m_planetQuery = {};

    bool m_dirty = false;
    DebugImageTexture m_mapTexture;

    int m_projIndex = 0;
    int m_layerIndex = 0;
    int m_resolution = 256;
    int m_lod = 0;

    PlanetTerrainParams m_appliedParams{};

    std::vector<float> m_values;
    std::vector<uint8_t> m_pixels;
    glm::ivec2 m_imageSize{0, 0};
    float m_valueMin = 0.0f;
    float m_valueMax = 0.0f;
    bool m_imageValid = false;
    double m_lastGenerationMs = 0.0;

    bool m_needsRegen = true;
    bool m_textureDirty = false;

    PlanetDirectionField m_field = {};
    std::vector<float> m_tileValues;
    std::vector<PlanetLayerProjectionTile> m_tiles;
};
} // namespace vp
