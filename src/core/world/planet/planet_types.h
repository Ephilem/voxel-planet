#pragma once
#include <cstdint>

namespace vp {
    enum CubemapFace : uint8_t {
        FACE_POS_X = 0,
        FACE_NEG_X = 1,
        FACE_POS_Y = 2,
        FACE_NEG_Y = 3,
        FACE_POS_Z = 4,
        FACE_NEG_Z = 5,

        FACE_UNKNOWN = 0xFF
    };

    inline const char *face_name(CubemapFace face) {
        switch (face) {
            case FACE_POS_X:
                return "+X";
            case FACE_NEG_X:
                return "-X";
            case FACE_POS_Y:
                return "+Y";
            case FACE_NEG_Y:
                return "-Y";
            case FACE_POS_Z:
                return "+Z";
            case FACE_NEG_Z:
                return "-Z";
            default:
                return "??";
        }
    }
}
