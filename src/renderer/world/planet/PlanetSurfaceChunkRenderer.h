#pragma once

#include "PlanetSurfaceChunkBuffer.h"
#include "renderer/IRenderPass.h"
#include <nvrhi/nvrhi.h>

namespace vp {
class PlanetSurfaceChunkRenderer : public IRenderPass {
public:
    PlanetSurfaceChunkRenderer() { init_gpu(); }

    ~PlanetSurfaceChunkRenderer() = default;

    void render(nvrhi::CommandListHandle cmd, Camera3d& camera, VulkanBackend& backend) override;

private:
    void init_gpu();

    std::unique_ptr<PlanetSurfaceChunkBuffer> m_chunkBuffer;
};
} // namespace vp
