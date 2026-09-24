#pragma once

#include <array>
#include <flecs.h>
#include <string>

#include "client/debug/IDebugPanel.h"
#include "client/world/planet/planet_client_components.h"
#include "core/world/planet/planet_components.h"
#include "renderer/world/planet/planet_rendering_components.h"

namespace vp {
/**
 * Select an planet, and show information and debug action for :
 * - Tiles Atlas
 * - Tiles Generations
 * - Tiles uploading
 */
class PlanetTilesDebug : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;

    const std::string name() const override { return "Planet Tiles"; }

    const std::string category() const override { return "Rendering"; }

private:
    struct PlanetRepresentation {
        flecs::entity entity = flecs::entity::null();

        flecs::ref<const PlanetTileLodComp> lod;
        flecs::ref<const PlanetTileStreamComp> stream;
        flecs::ref<const PlanetTileDrawListComp> drawList;
    };

    // Planet selection
    flecs::query<const Planet, const PlanetTileLodComp, const PlanetTileStreamComp> m_planetQuery;
    PlanetRepresentation m_selectedPlanet = {};

    void select_planet(PlanetRepresentation planet);
    void collect_planets(flecs::world& ecs, std::vector<PlanetRepresentation>& planets);

    void draw_lod();
    void draw_generator();
    void draw_atlas(flecs::world& ecs);
};
} // namespace vp
