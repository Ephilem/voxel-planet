#pragma once

#include <flecs.h>

#include "spatial_components.h"
#include "core/log/Logger.h"

namespace vp {
    /**
     * Find the first ancestor of the given entity that has a Grid component.
     * This is used to find the grid that a cell or other spatial-specific entity belongs to.
     * If no ancestor with a Grid component is found, this function will log a fatal error and abort the program.
     * @param e
     * @return
     */
    inline flecs::entity get_first_ancestor_grid(flecs::entity e) {
        flecs::entity current = e.parent();
        while (current.is_valid()) {
            if (current.has<Grid>()) {
                return current;
            }
            current = current.parent();
        }
        LOG_FATAL("Spatial", "A entity with a CellCoord or other spatial specifics component MUST be child of a grid. No ancestor with Grid component found for entity {}", e.id());
        std::abort();
    }

    /**
     * Get the HP position of an entity that is a parent of a grid.
     * Will get the parent, the grid and his CellCoord and Transform to calculate the HP position of the entity in the grid space
     * @param entity the entity. It should be a child of a grid, and have a CellCoord and Transform component.
     * @return return calculated position. If it doesnt have a parent with a Grid component, return only the transform position as fallback
     */
    inline glm::dvec3 get_hp_position(flecs::entity entity) {
        const Transform* t = entity.get<Transform>();
        const CellCoord* c = entity.get<CellCoord>();
        flecs::entity parent = entity.parent();
        if (!parent.is_valid()) return t ? glm::dvec3(t->pos) : glm::dvec3(0.0);
        const Grid* grid = parent.get<Grid>();
        if (!grid) return glm::dvec3(0.0);

        return grid->get_hp_grid_pos(*c, t->pos);

    }
}
