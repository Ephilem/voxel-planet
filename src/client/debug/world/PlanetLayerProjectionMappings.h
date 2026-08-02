//
// Created by raph on 04/08/2026.
//
#pragma once
#include <glm/vec2.hpp>

#include <string>
#include <vector>

#include "PlanetDirectionField.h"
#include "core/world/planet/planet_types.h"

namespace vp {
    /**
     * Represent a tile where a sampling operation will be executed
     * For example, a tile can be a face of a cubemap or a full rectangle in an equirectangular projection
     */
    struct PlanetLayerProjectionTile {
        glm::ivec2 origin{0, 0}; // top-left position of the tile on the final image
        glm::ivec2 size{0, 0}; // size of the tile

        // supplementary information
        CubemapFace face = FACE_UNKNOWN;
    };

    /**
     * Map an sphere onto an 2d image. only known the geometry: what to map and where to sample
     * It doesn't know how to sample tho
     */
    class PlanetLayerProjectionMapping {
    public:
        virtual ~PlanetLayerProjectionMapping() = default;

        virtual const char *name() const = 0;

        /**
         * Final canvas size
         *
         * @param resolution pixel per cubeface (90 x 90 degrees).
         *        Projections without faces convert to a equatorial density
         */
        virtual glm::ivec2 image_size(int resolution) const = 0;

        /**
         * Cut to sampleable region. The sum area is <= to image_size
         */
        virtual void tiles(int resolution, std::vector<PlanetLayerProjectionTile> &out) const = 0;

        /**
         * Fill tile of direction to sample
         * @param out Field of direction
         */
        virtual void fill_tile_directions(const PlanetLayerProjectionTile &tile, PlanetDirectionField &out) const = 0;

        /// Tooltip text for a pixel in the final image. Can be empty
        virtual std::string describe_pixel(int x, int y, const glm::ivec2 &imageSize) const = 0;

        /// ImPlot axis ranges. Y is inverted when the image origin is top-left.
        virtual void plot_bounds(const glm::ivec2 &imageSize,
                                 double &xMin, double &xMax,
                                 double &yMin, double &yMax) const = 0;

        /// nullptr = axe without graduation
        virtual const char *x_axis_label() const { return nullptr; }
        virtual const char *y_axis_label() const { return nullptr; }

        virtual void draw_overlay(const glm::ivec2 &imageSize) const {
        }
    };

    const PlanetLayerProjectionMapping &equirectangular_projection();
    const PlanetLayerProjectionMapping &cube_cross_projection();
    const PlanetLayerProjectionMapping *projection_at(int index);
    int projection_count();
}
