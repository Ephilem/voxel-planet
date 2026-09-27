#pragma once

#include <flecs.h>
#include <string>

#include "client/debug/IDebugPanel.h"
#include "core/world/planet/PlanetSurfaceChunkStore.h"

namespace vp {

/**
 * Follow the surface chunks through store -> mesher -> GPU buffer, to spot where chunks get lost
 */
class PlanetChunksRenderingDebug : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;

    const std::string name() const override { return "Planet Chunks Rendering"; }

    const std::string category() const override { return "Rendering"; }

private:
    flecs::query<const PlanetSurfaceChunkStore> m_storeQuery;
};

} // namespace vp
