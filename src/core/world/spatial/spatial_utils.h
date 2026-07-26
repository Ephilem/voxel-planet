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


    /**
     * Project a position from the child grid to the parent grid using the transition defined on the child grid.
     * Optionnally, compute the Jacobian of the transformation at that point
     * @param transition transition describing how the child grid maps into its parent
     * @param pos absolute position in the local child space
     * @param outJ If non-null, filled with the jacobian d(pos)/d(x,y,z) of the projection at pos
     * @return the absolute projected position in the parent space
     */
    inline glm::dvec3 project(const GridTransition& transition, const glm::dvec3& pos, glm::dmat3* outJ = nullptr) {
        if (transition.kind == TransitionKind::Linear) {
            if (outJ)
                *outJ = glm::dmat3(1.0);

            return pos;
        }

        const double R = transition.radius;
        const glm::dvec3 ex = transition.faceBasis[0];
        const glm::dvec3 en = transition.faceBasis[1];
        const glm::dvec3 ez = transition.faceBasis[2];

        const glm::dvec3 u = ex * pos.x + en * R + ez * pos.z;
        const double r2 = glm::dot(u, u);
        const double r  = std::sqrt(r2);
        const glm::dvec3 n = u / r;
        const double h = R + pos.y;

        if (outJ) {
            const double k = h / r;
            (*outJ)[0] = k * (ex - (pos.x / r2) * u);
            (*outJ)[1] = n;
            (*outJ)[2] = k * (ez - (pos.z / r2) * u);
        }

        return n * h;
    }

    /**
     * Get the quaternion representing a rotation from the jacobian matrice get in the project method
     * @param J The jacobian matrice
     * @return
     */
    inline glm::dquat jacobian_to_quat(const glm::dmat3& J) {
        const glm::dvec3 up    = glm::normalize(J[1]);
        const glm::dvec3 right = glm::normalize(J[0] - up * glm::dot(J[0], up));
        const glm::dvec3 fwd   = glm::cross(right, up);
        return glm::quat_cast(glm::dmat3(right, up, fwd));
    }
}
