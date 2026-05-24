#pragma once

#include <flecs.h>
#include <memory>
#include <glm/vec3.hpp>

#include "core/world/planet/PlanetChunkGenerator.h"
#include "core/world/planet/planet_components.h"
#include "core/world/spatial/spatial_components.h"

namespace vp {
    /**
     * class that manage what to show and how to show the surface of the world to the client
     */
    class SurfaceWindowManager {
    public:
        SurfaceWindowManager() = default;
        ~SurfaceWindowManager() = default;
        SurfaceWindowManager(const SurfaceWindowManager&&) = default;
        SurfaceWindowManager& operator=(SurfaceWindowManager&&) = default;

        static void Register(flecs::world& ecs);

    private:
        std::unique_ptr<PlanetChunkGenerator> m_generator;

        void init(flecs::world& ecs);

        /**
         * @param planet where to construct the anchor
         * @param coords position of the player on the planet, in planetary relative coordinate
         */
        void initialize_anchor(flecs::entity planet, SpatialCoordinate coords);

        void create_chunk(flecs::entity anchor, int chunkU, int chunkV, int alt);
        void destroy_chunk(flecs::entity anchor, int chunkU, int chunkV, int alt);

        void reset_window(flecs::entity anchor, glm::dvec3 playerPlanetPos);

        void check_grid_shift(flecs::entity anchor, SurfaceAnchorComp& a, glm::dvec3 newCenter);
        void set_anchor(flecs::entity anchor, SurfaceAnchorComp& a, glm::dvec3 playerPlanetPos);

        // -- Ecs systems
        void on_player_entered_surface(flecs::entity player, flecs::entity planet);
        void on_player_exited_surface(flecs::entity player, flecs::entity planet);

        void system_poll_chunk_generation();
        void system_update_surface_window(flecs::entity anchor, SurfaceAnchorComp a, const Transform& playerTrs, const CellCoord& playerCell);
    };
}
