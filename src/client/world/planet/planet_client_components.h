#pragma once

#include <memory>

#include "PlanetQuadtrees.h"

namespace vp {
    struct PlanetTileLodComp {
        std::unique_ptr<PlanetQuadtrees> quadtree;
        PlanetLodParams params;

        bool debugDrawNodes = true;
        PlanetQuadtrees::DebugMode debugMode = PlanetQuadtrees::DebugMode::Level;
        int debugSegmentsPerEdge = 6;
    };
}
