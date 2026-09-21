
#include <flecs.h>
#include <string>

#include "client/debug/IDebugPanel.h"
#include "core/world/planet/planet_components.h"
#include "core/world/planet/PlanetSurfaceChunkGenerator.h"
#include "core/world/planet/PlanetSurfaceChunkStore.h"

namespace vp {

/**
 * Select an planet, and show information and debug action for :
 * - Chunk stores
 * - Chunk generators
 */
class PlanetChunksDebug : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;

    const std::string name() const override { return "Planet Chunks"; }

    const std::string category() const override { return "World"; }

private:
    struct PlanetRepresentation {
        flecs::entity entity = flecs::entity::null();

        flecs::ref<const PlanetSurfaceChunkStore> store;
        flecs::ref<const PlanetSurfaceChunkGeneratorComp> generator;
    };

    // Planet selection
    flecs::query<const Planet, const PlanetSurfaceChunkStore, const PlanetSurfaceChunkGeneratorComp> m_planetQuery;
    PlanetRepresentation m_selectedPlanet = {};

    void select_planet(PlanetRepresentation planet);
    void collect_planets(flecs::world& ecs, std::vector<PlanetRepresentation>& planets);
};
} // namespace vp
