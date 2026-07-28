#pragma once


namespace vp {
    inline static float equiangular(float s) {
        return std::tan(s /** (static_cast<float>(M_PI) / 4.0f)*/);
    }
}
