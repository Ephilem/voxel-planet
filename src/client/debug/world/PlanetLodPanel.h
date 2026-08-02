#pragma once
#include "../IDebugPanel.h"

/**
 * Tuning and live stats for the planet quadtree LOD.
 *
 * The tree state it shows is the one the renderer never sees: nodes waiting on a
 * generation, or holding no slot at all, are invisible in the final image and in the
 * wireframe alike, which is what makes a stalled refinement so hard to read
 */
class PlanetLodPanel : public IDebugPanel {
public:
    void render(flecs::world& ecs) override;

    const std::string name() const override { return "Planet LOD"; }
    const std::string category() const override { return "World"; }
};
