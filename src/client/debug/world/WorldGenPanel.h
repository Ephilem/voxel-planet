#pragma once

#include "../IDebugPanel.h"

#include <climits>
#include <vector>
#include <nvrhi/nvrhi.h>
#include <vulkan/vulkan.h>
#include <imgui.h>

#include "core/world/WorldGenerator.h"

struct Renderer;

class WorldGenPanel : public IDebugPanel {
public:
    ~WorldGenPanel();

    void render(flecs::world &ecs) override;

    const std::string name() const override { return "World Generator"; }
    const std::string category() const override { return "World"; }

private:
    WorldGenerator::NoiseParams m_pending;
    bool m_paramsInitialized = false;

    enum class ApplyState { Idle, WaitingDrain };

    ApplyState m_applyState = ApplyState::Idle;

    WorldGenerator m_previewGen;
    static constexpr int PREVIEW_SIZE = 256;
    std::vector<uint8_t> m_pixels; // RGBA, PREVIEW_SIZE × PREVIEW_SIZE
    bool m_previewDirty = true;
    int  m_previewCenterX  = 0;
    int  m_previewCenterZ  = 0;
    int  m_previewWorldSize = 512;
    bool m_followPlayer    = true;
    int  m_playerChunkX    = INT_MIN;  // dernier chunk connu du joueur
    int  m_playerChunkZ    = INT_MIN;

    nvrhi::TextureHandle     m_previewTexture;
    nvrhi::CommandListHandle m_uploadCmdList;  // command list dédié avec UploadManager
    VkSampler                m_previewSampler = VK_NULL_HANDLE;
    ImTextureID              m_previewTexId   = 0;
    VkDevice                 m_cachedVkDevice = VK_NULL_HANDLE;

    void render_params(WorldGenerator* gen);

    void render_preview(flecs::world &ecs, const Renderer* renderer);

    void init_texture(const Renderer* renderer);

    void destroy_texture();

    void update_preview_pixels();

    void upload_preview_texture(const Renderer* renderer);

    static bool param_float(const char* label, float* v, float vMin, float vMax, const char* fmt = "%.4f");

    static bool param_int(const char* label, int* v, int vMin, int vMax);

    static ImVec4 height_to_color(float t);
};
