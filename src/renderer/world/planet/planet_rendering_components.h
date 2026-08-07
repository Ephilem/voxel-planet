#pragma once
#include <vector>

#include <glm/glm.hpp>

#include "core/world/planet/planet_types.h"

namespace vp {
    struct alignas(16) PlanetTileDrawInstance {
        glm::vec3 originSpacePos;
        float extent;
        glm::vec2 nodeFaceOrigin;

        /// face (3 bits) | level (5 bits)
        uint32_t packed;

        /// 0 = own resolution, 1 = snapped to the parent
        float morph;
    };

    inline uint32_t planet_tile_pack(CubemapFace face, uint8_t level) {
        return uint32_t(face) | (uint32_t(level) << 3);
    }

    struct PlanetTileDrawList {
        std::vector<PlanetTileDrawInstance> drawInstances;
    };
}
