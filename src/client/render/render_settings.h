#pragma once

namespace vp::client {
/// Scene rendering settings, singleton owned by SceneRenderModule
struct RenderingPreferences {
    // Chunk drawing
    float chunkFadeStart = 8 * 32; // in meter
    float chunkFadeEnd = 10 * 32; // in meter
};
} // namespace vp::client
