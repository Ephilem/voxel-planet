#pragma once

#include <flecs.h>
#include <unordered_map>
#include <unordered_set>

#include "planet_components.h"
#include "core/world/world_components.h"

namespace vp {

    class PlanetChunkManager {
    public:
        PlanetChunkManager();
        void init(flecs::world& ecs);
        static void Register(flecs::world& ecs);

    private:
        struct PlanetRuntime {
            std::unordered_map<PlanetChunkCoord, flecs::entity, PlanetChunkCoordHash> loadedChunks;
            std::unordered_set<PlanetChunkCoord, PlanetChunkCoordHash> loadingChunks;
            std::unordered_set<PlanetChunkCoord, PlanetChunkCoordHash> emptyChunks;
            std::vector<PlanetChunkCoord> candidateHeapChunks;
        };

        std::unordered_map<flecs::entity_t, PlanetRuntime> m_planets;

        // systems
        void on_planet_created(flecs::entity e);
        void on_planet_removed(flecs::entity e);

        /**
         * Update the chunk loader for a planet. this will enqueue candidate chunks to load, and unload chunks that are no longer needed.
         * @param e The entity chunk loader
         * @param planet the planet where he load chunk (generally his parent
         * @param loader the loader configuration
         */
        void system_update_chunks(flecs::entity e, flecs::entity planet, const ChunkLoader &loader);

        void system_poll_results(flecs::entity ePlanet);

        void system_process_unload(flecs::iter& it);

        void system_drain_candidates();


    };
}
