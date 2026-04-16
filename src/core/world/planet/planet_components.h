#pragma once

namespace vp {


    struct PlanetChunkCoord {
        vp::CubeFace face;
        int x;
        int y;
        int altitude;

        bool operator==(PlanetChunkCoord const& other) const {
            return face == other.face && x == other.x && y == other.y && altitude == other.altitude;
        }
    };

    struct PlanetComp {
        // world unit (meter)
        float radius = 6371.0f;

    };


}