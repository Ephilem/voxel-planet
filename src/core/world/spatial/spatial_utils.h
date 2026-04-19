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


}
