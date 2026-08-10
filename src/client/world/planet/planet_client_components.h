#pragma once

#include <memory>

#include "PlanetQuadtrees.h"

namespace vp {
    struct PlanetTileLodComp {
        std::unique_ptr<PlanetQuadtrees> quadtree;
        PlanetLodParams params;

        /// Filters the draw list against the camera frustum. Toggleable so the debug view
        /// can freeze the traversal and still fly around to inspect what was culled
        bool frustumCulling = true;

        bool debugDrawNodes = false;
        PlanetQuadtrees::DebugMode debugMode = PlanetQuadtrees::DebugMode::Level;
        int debugSegmentsPerEdge = 6;
    };
}
