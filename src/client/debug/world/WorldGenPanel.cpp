#include "WorldGenPanel.h"

#include <imgui.h>
#include <imgui_impl_vulkan.h>

#include <vulkan/vulkan.hpp>

#include "core/TracyIntegration.h"
#include "core/world/ChunkManager.h"
#include "core/main_components.h"
#include "renderer/Renderer.h"
#include "renderer/vulkan/VulkanBackend.h"
#include "renderer/rendering_components.h"

WorldGenPanel::~WorldGenPanel() {
    destroy_texture();
}

void WorldGenPanel::render(flecs::world &ecs) {
    VOXEL_ZONE_N("WorldGenPanel-Render");

    auto* gen = ecs.get_mut<WorldGenerator>();
    auto* renderer = ecs.get<Renderer>();
    if (!gen || !renderer) return;

    if (!m_paramsInitialized) {
        m_pending = gen->get_params();
        m_paramsInitialized = true;
    }

    if (!m_previewTexture && renderer->backend) {
        init_texture(renderer);
    }

    if (m_followPlayer) {
        ecs.each([this](const Camera3d &, const Transform &transform) {
            // Ne recalcule que si le joueur a changé de chunk
            const int cx = static_cast<int>(std::floor(transform.pos.x / CHUNK_SIZE));
            const int cz = static_cast<int>(std::floor(transform.pos.z / CHUNK_SIZE));
            if (cx != m_playerChunkX || cz != m_playerChunkZ) {
                m_playerChunkX   = cx;
                m_playerChunkZ   = cz;
                m_previewCenterX = static_cast<int>(transform.pos.x);
                m_previewCenterZ = static_cast<int>(transform.pos.z);
                m_previewDirty   = true;
            }
        });
    }

    if (ImGui::BeginTable("##wg_layout", 2, ImGuiTableFlags_BordersInnerV)) {
        ImGui::TableSetupColumn("##params",  ImGuiTableColumnFlags_WidthFixed,   320.0f);
        ImGui::TableSetupColumn("##preview", ImGuiTableColumnFlags_WidthStretch, 0.0f);

        ImGui::TableNextColumn();
        render_params(gen);

        ImGui::TableNextColumn();
        render_preview(ecs, renderer);

        ImGui::EndTable();
    }
}

void WorldGenPanel::render_params(WorldGenerator* gen) {
    ImGui::SeparatorText("Noise Parameters");

    bool changed = false;
    changed |= param_float("Frequency", &m_pending.frequency, 0.001f, 0.1f, "%.5f");
    changed |= param_int("Octaves", &m_pending.octaves, 1, 12);
    changed |= param_float("Lacunarity", &m_pending.lacunarity, 1.0f, 4.0f, "%.2f");
    changed |= param_float("Gain", &m_pending.gain, 0.1f, 1.0f, "%.2f");

    ImGui::SeparatorText("Terrain Shape");
    changed |= param_int("Base Height", &m_pending.baseHeight, 0, 512);
    changed |= param_int("Amplitude", &m_pending.heightAmplitude, 1, 256);

    if (changed) m_previewDirty = true;

    ImGui::Spacing();
    ImGui::SeparatorText("Apply");

    const bool paramsMatch = (m_pending.frequency == gen->get_params().frequency &&
                              m_pending.octaves == gen->get_params().octaves &&
                              m_pending.lacunarity == gen->get_params().lacunarity &&
                              m_pending.gain == gen->get_params().gain &&
                              m_pending.baseHeight == gen->get_params().baseHeight &&
                              m_pending.heightAmplitude == gen->get_params().heightAmplitude);

    if (m_applyState == ApplyState::WaitingDrain) {
        ImGui::TextColored({1.0f, 1.0f, 0.0f, 1.0f}, "Draining workers...");
    }

    ImGui::BeginDisabled(m_applyState != ApplyState::Idle);
    if (ImGui::Button("Apply & Reload World", {-1, 0})) {
        m_applyState = ApplyState::WaitingDrain;
    }
    ImGui::EndDisabled();

    if (paramsMatch) {
        ImGui::TextDisabled("World is up to date.");
    } else {
        ImGui::TextColored({1.0f, 0.6f, 0.2f, 1.0f}, "Pending changes not applied.");
    }

    ImGui::Spacing();

    if (ImGui::Button("Reset to Current", {-1, 0})) {
        m_pending = gen->get_params();
        m_previewDirty = true;
    }
}

void WorldGenPanel::render_preview(flecs::world &ecs, const Renderer* renderer) {
    if (m_applyState == ApplyState::WaitingDrain) {
        auto* cm = ecs.get_mut<ChunkManager>();
        auto* gen = ecs.get_mut<WorldGenerator>();
        if (cm && gen) {
            cm->enqueueCandidates = false;
            cm->loadingEnabled = false;
            if (cm->get_stats().loadingChunkCount == 0) {
                gen->set_params(m_pending);
                cm->unload_all_chunks(ecs);
                cm->enqueueCandidates = true;
                cm->loadingEnabled = true;
                m_applyState = ApplyState::Idle;
            }
        }
    }

    // Preview controls
    ImGui::SeparatorText("Preview");

    if (ImGui::Checkbox("Follow Player", &m_followPlayer)) {
        m_previewDirty = true;
    }

    if (!m_followPlayer) {
        bool moved = false;
        moved |= ImGui::DragInt("Center X", &m_previewCenterX, 1.0f);
        moved |= ImGui::DragInt("Center Z", &m_previewCenterZ, 1.0f);
        if (moved) m_previewDirty = true;
    } else {
        ImGui::TextDisabled("Center: (%d, %d)", m_previewCenterX, m_previewCenterZ);
    }

    if (param_int("World Size", &m_previewWorldSize, 64, 4096)) {
        m_previewDirty = true;
    }

    // Update + upload
    if (m_previewDirty) {
        update_preview_pixels();
        if (m_previewTexture && renderer->frameContext.commandList) {
            upload_preview_texture(renderer);
        }
        m_previewDirty = false;
    }

    // Display
    if (m_previewTexId) {
        const float avail = ImGui::GetContentRegionAvail().x;
        const float size  = std::min(avail, static_cast<float>(PREVIEW_SIZE));
        ImGui::Image(m_previewTexId, ImVec2(size, size));

        const ImVec2 imgMin = ImGui::GetItemRectMin();
        const ImVec2 imgMax = ImGui::GetItemRectMax();

        // --- Grille de chunks ---
        const float worldLeft = static_cast<float>(m_previewCenterX) - m_previewWorldSize * 0.5f;
        const float worldTop  = static_cast<float>(m_previewCenterZ) - m_previewWorldSize * 0.5f;
        const float pixelsPerWorld = size / static_cast<float>(m_previewWorldSize);
        const float chunkPixels    = CHUNK_SIZE * pixelsPerWorld;

        // N'affiche la grille que si les lignes sont espacées d'au moins 4px
        if (chunkPixels >= 4.0f) {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            constexpr ImU32 COL_GRID    = IM_COL32(255, 255, 255,  55);
            constexpr ImU32 COL_PLAYER  = IM_COL32(255, 210,  50, 220);

            // Lignes verticales (X)
            const float firstX = std::ceil(worldLeft / CHUNK_SIZE) * CHUNK_SIZE;
            for (float wx = firstX; wx <= worldLeft + m_previewWorldSize + CHUNK_SIZE; wx += CHUNK_SIZE) {
                float sx = imgMin.x + (wx - worldLeft) * pixelsPerWorld;
                if (sx >= imgMin.x && sx <= imgMax.x)
                    dl->AddLine(ImVec2(sx, imgMin.y), ImVec2(sx, imgMax.y), COL_GRID, 1.0f);
            }

            // Lignes horizontales (Z)
            const float firstZ = std::ceil(worldTop / CHUNK_SIZE) * CHUNK_SIZE;
            for (float wz = firstZ; wz <= worldTop + m_previewWorldSize + CHUNK_SIZE; wz += CHUNK_SIZE) {
                float sz = imgMin.y + (wz - worldTop) * pixelsPerWorld;
                if (sz >= imgMin.y && sz <= imgMax.y)
                    dl->AddLine(ImVec2(imgMin.x, sz), ImVec2(imgMax.x, sz), COL_GRID, 1.0f);
            }

            // Chunk du joueur mis en évidence
            const float pcx = std::floor(static_cast<float>(m_previewCenterX) / CHUNK_SIZE) * CHUNK_SIZE;
            const float pcz = std::floor(static_cast<float>(m_previewCenterZ) / CHUNK_SIZE) * CHUNK_SIZE;
            const float sx0 = imgMin.x + (pcx - worldLeft) * pixelsPerWorld;
            const float sz0 = imgMin.y + (pcz - worldTop)  * pixelsPerWorld;
            dl->AddRect(ImVec2(sx0, sz0), ImVec2(sx0 + chunkPixels, sz0 + chunkPixels),
                        COL_PLAYER, 0.0f, 0, 1.5f);
        }

        // --- Tooltip ---
        if (ImGui::IsItemHovered()) {
            const ImVec2 mouse = ImGui::GetMousePos();
            const float  u  = (mouse.x - imgMin.x) / size;
            const float  v  = (mouse.y - imgMin.y) / size;
            const int    wx = m_previewCenterX + static_cast<int>((u - 0.5f) * m_previewWorldSize);
            const int    wz = m_previewCenterZ + static_cast<int>((v - 0.5f) * m_previewWorldSize);
            const int    cx = static_cast<int>(std::floor(static_cast<float>(wx) / CHUNK_SIZE));
            const int    cz = static_cast<int>(std::floor(static_cast<float>(wz) / CHUNK_SIZE));
            ImGui::SetTooltip("World  (%d, %d)\nChunk  (%d, %d)", wx, wz, cx, cz);
        }
    } else {
        ImGui::TextDisabled("Texture not ready.");
    }
}

void WorldGenPanel::update_preview_pixels() {
    // Scale frequency so that PREVIEW_SIZE pixels cover m_previewWorldSize world units.
    // GenUniformGrid2D samples at (startX + x) * frequency.
    // We want pixel x to sample at (centerX - W/2 + x * step) * realFreq.
    // Solution: scale start coords down by step and multiply frequency by step.
    const int step = std::max(1, m_previewWorldSize / PREVIEW_SIZE);
    const int startX = (m_previewCenterX - m_previewWorldSize / 2) / step;
    const int startZ = (m_previewCenterZ - m_previewWorldSize / 2) / step;

    WorldGenerator::NoiseParams previewParams = m_pending;
    previewParams.frequency = m_pending.frequency * static_cast<float>(step);
    m_previewGen.set_params(previewParams);

    std::vector<float> heights(PREVIEW_SIZE * PREVIEW_SIZE);
    m_previewGen.sample_heightmap(startX, startZ, PREVIEW_SIZE, PREVIEW_SIZE, heights.data());

    // Normalize to [0, 1]
    float minH = heights[0], maxH = heights[0];
    for (float h: heights) {
        minH = std::min(minH, h);
        maxH = std::max(maxH, h);
    }
    const float range = (maxH > minH) ? (maxH - minH) : 1.0f;

    m_pixels.resize(PREVIEW_SIZE * PREVIEW_SIZE * 4);
    for (int i = 0; i < PREVIEW_SIZE * PREVIEW_SIZE; i++) {
        float t = (heights[i] - minH) / range;
        ImVec4 c = height_to_color(t);
        m_pixels[i * 4 + 0] = static_cast<uint8_t>(c.x * 255.0f);
        m_pixels[i * 4 + 1] = static_cast<uint8_t>(c.y * 255.0f);
        m_pixels[i * 4 + 2] = static_cast<uint8_t>(c.z * 255.0f);
        m_pixels[i * 4 + 3] = 255;
    }
}


void WorldGenPanel::init_texture(const Renderer* renderer) {
    auto* device = renderer->backend->device.Get();

    nvrhi::TextureDesc desc;
    desc.width = PREVIEW_SIZE;
    desc.height = PREVIEW_SIZE;
    desc.format = nvrhi::Format::RGBA8_UNORM;
    desc.initialState = nvrhi::ResourceStates::ShaderResource;
    desc.keepInitialState = true;
    desc.debugName = "WorldGenPreview";
    m_previewTexture = device->createTexture(desc);

    // Command list dédié avec un UploadManager pour writeTexture.
    // Le command list du frame principal est créé sans uploadChunkSize (= 0),
    // donc son m_UploadManager est null → crash si on appelle writeTexture dessus.
    nvrhi::CommandListParameters uploadParams;
    uploadParams.uploadChunkSize = PREVIEW_SIZE * PREVIEW_SIZE * 4 * 2; // 2× la taille de la texture
    m_uploadCmdList = device->createCommandList(uploadParams);

    VkDevice vkDevice = renderer->backend->vkDevice;
    m_cachedVkDevice = vkDevice;

    VkSamplerCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    si.magFilter = VK_FILTER_NEAREST;
    si.minFilter = VK_FILTER_NEAREST;
    si.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(vkDevice, &si, nullptr, &m_previewSampler);

    VkImageView imageView = m_previewTexture->getNativeView(
        nvrhi::ObjectTypes::VK_ImageView,
        nvrhi::Format::RGBA8_UNORM,
        nvrhi::TextureSubresourceSet(0, 1, 0, 1),
        nvrhi::TextureDimension::Texture2D
        );
    VkDescriptorSet ds = ImGui_ImplVulkan_AddTexture(
        m_previewSampler, imageView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    m_previewTexId = reinterpret_cast<ImU64>(ds);
}

void WorldGenPanel::destroy_texture() {
    if (m_previewTexId) {
        if (m_previewTexId != 0) {
            ImGui_ImplVulkan_RemoveTexture(reinterpret_cast<VkDescriptorSet>(m_previewTexId));
            m_previewTexId = 0;
        }
    }
    if (m_previewSampler != VK_NULL_HANDLE && m_cachedVkDevice != VK_NULL_HANDLE) {
        vkDestroySampler(m_cachedVkDevice, m_previewSampler, nullptr);
        m_previewSampler = VK_NULL_HANDLE;
        m_cachedVkDevice = VK_NULL_HANDLE;
    }
    m_previewTexture = nullptr;
}

void WorldGenPanel::upload_preview_texture(const Renderer* renderer) {
    m_uploadCmdList->open();
    m_uploadCmdList->writeTexture(
        m_previewTexture, 0, 0,
        m_pixels.data(),
        PREVIEW_SIZE * 4  // row pitch en bytes
    );
    m_uploadCmdList->close();
    renderer->backend->device->executeCommandList(m_uploadCmdList);
}

bool WorldGenPanel::param_float(const char* label, float* v, float vMin, float vMax, const char* fmt) {
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    return ImGui::SliderFloat(label, v, vMin, vMax, fmt);
}

bool WorldGenPanel::param_int(const char* label, int* v, int vMin, int vMax) {
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.6f);
    return ImGui::SliderInt(label, v, vMin, vMax);
}

ImVec4 WorldGenPanel::height_to_color(float t) {
    // 5-stop terrain gradient
    struct Stop {
        float t;
        ImVec4 c;
    };
    static constexpr Stop stops[] = {
        {0.00f, {0.05f, 0.10f, 0.45f, 1}}, // deep water
        {0.35f, {0.85f, 0.80f, 0.55f, 1}}, // sand
        {0.50f, {0.25f, 0.60f, 0.20f, 1}}, // grass
        {0.75f, {0.45f, 0.42f, 0.38f, 1}}, // rock
        {1.00f, {0.95f, 0.95f, 1.00f, 1}}, // snow
    };
    constexpr int N = sizeof(stops) / sizeof(stops[0]);

    for (int i = 0; i < N - 1; i++) {
        if (t <= stops[i + 1].t) {
            float f = (t - stops[i].t) / (stops[i + 1].t - stops[i].t);
            const ImVec4 &a = stops[i].c;
            const ImVec4 &b = stops[i + 1].c;
            return {
                a.x + f * (b.x - a.x),
                a.y + f * (b.y - a.y),
                a.z + f * (b.z - a.z), 1.0f
            };
        }
    }
    return stops[N - 1].c;
}
