#include "VoxelBufferVisualizer.h"
#include "imgui.h"
#include "renderer/Renderer.h"
#include "renderer/world/VoxelTerrainRenderer.h"
#include "renderer/world/VoxelBuffer.h"
#include <algorithm>

void VoxelBufferVisualizer::register_ecs(flecs::world &ecs) {
    ecs.system<Renderer>("VoxelBufferVisualizer-DisplaySystem")
        .kind(flecs::PreStore)
        .each([this](flecs::iter& it, size_t, Renderer& renderer) {
            if (!this->m_visible) return;
            if (!renderer.voxelTerrainRenderer) return;

            this->draw(renderer.voxelTerrainRenderer.get());
        });
}

void VoxelBufferVisualizer::draw(VoxelTerrainRenderer* voxelRenderer) {
    ImGui::SetNextWindowSize(ImVec2(900, 600), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Voxel Buffer Visualizer", &m_visible)) {
        const auto& buffers = voxelRenderer->get_voxel_buffers();

        if (buffers.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "No voxel buffers allocated yet");
            ImGui::End();
            return;
        }

        ImGui::Text("Total Buffers: %zu", buffers.size());
        ImGui::Separator();

        ImGui::Spacing();
        ImGui::Separator();

        ImGui::Text("64MB Face Buffer Memory Map");
        ImGui::Spacing();

        float availWidth = ImGui::GetContentRegionAvail().x;
        draw_memory_map(voxelRenderer, availWidth, 120.0f);

        ImGui::Spacing();
        ImGui::Separator();

        draw_statistics(voxelRenderer);

        ImGui::Spacing();
        ImGui::Separator();

        draw_fragmentation_info(voxelRenderer);
    }
    ImGui::End();
}

void VoxelBufferVisualizer::draw_memory_map(VoxelTerrainRenderer* voxelRenderer, float width, float height) {
    const auto& buffers = voxelRenderer->get_voxel_buffers();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();

    float bufferHeight = height / std::max(1.0f, static_cast<float>(buffers.size()));
    float spacing = 2.0f;

    for (size_t bufIdx = 0; bufIdx < buffers.size(); bufIdx++) {
        const auto& buffer = buffers[bufIdx];
        float yOffset = canvas_pos.y + bufIdx * bufferHeight;
        float sectionHeight = bufferHeight - spacing;

        char label[32];
        snprintf(label, sizeof(label), "Buffer %zu", bufIdx);
        draw_list->AddText(ImVec2(canvas_pos.x - 70, yOffset + sectionHeight * 0.3f),
                          IM_COL32(200, 200, 200, 255), label);

        draw_list->AddRectFilled(
            ImVec2(canvas_pos.x, yOffset),
            ImVec2(canvas_pos.x + width, yOffset + sectionHeight),
            IM_COL32(100, 160, 220, 255));

        const auto& freeFaceRegions = buffer.get_free_face_regions();
        for (const auto& [regionStart, regionCount] : freeFaceRegions) {
            float blockStart = canvas_pos.x + (static_cast<float>(regionStart) / MAX_FACES_REGIONS) * width;
            float blockWidth = (static_cast<float>(regionCount) / MAX_FACES_REGIONS) * width;

            draw_list->AddRectFilled(
                ImVec2(blockStart, yOffset),
                ImVec2(blockStart + blockWidth, yOffset + sectionHeight),
                IM_COL32(60, 60, 60, 255));

            draw_list->AddRect(
                ImVec2(blockStart, yOffset),
                ImVec2(blockStart + blockWidth, yOffset + sectionHeight),
                IM_COL32(100, 100, 100, 200), 0.0f, 0, 1.0f);
        }

        draw_list->AddRect(
            ImVec2(canvas_pos.x, yOffset),
            ImVec2(canvas_pos.x + width, yOffset + sectionHeight),
            IM_COL32(255, 255, 255, 128), 0.0f, 0, 1.5f);
    }

    draw_list->AddText(
        ImVec2(canvas_pos.x + width * 0.5f - 20, canvas_pos.y - 18),
        IM_COL32(255, 255, 255, 255), "FACES");

    ImGui::Dummy(ImVec2(width, height));

    ImGui::Spacing();
    ImGui::Text("Legend:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.39f, 0.63f, 0.86f, 1.0f), "■"); ImGui::SameLine();
    ImGui::Text("Faces Used");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.24f, 0.24f, 0.24f, 1.0f), "■"); ImGui::SameLine();
    ImGui::Text("Free (fragmentation)");
}

void VoxelBufferVisualizer::draw_statistics(VoxelTerrainRenderer* voxelRenderer) {
    const auto& buffers = voxelRenderer->get_voxel_buffers();

    ImGui::Text("Memory Usage Statistics");
    ImGui::Spacing();

    uint32_t totalUsedFaces = 0;
    uint32_t totalDrawCount = 0;

    for (const auto& buffer : buffers) {
        totalUsedFaces += buffer.get_used_face_regions();
        totalDrawCount += buffer.get_draw_count();
    }

    uint32_t totalMaxFaces = MAX_FACES_REGIONS * buffers.size();

    ImGui::Text("Face Buffer:");
    ImGui::SameLine(200);
    float faceUsage = totalMaxFaces > 0 ? static_cast<float>(totalUsedFaces) / totalMaxFaces : 0.0f;
    ImGui::ProgressBar(faceUsage, ImVec2(-1, 0), nullptr);
    ImGui::SameLine(0, 10);
    ImGui::Text("%.1f%%", faceUsage * 100.0f);

    ImGui::Text("  Used/Total Regions:");
    ImGui::SameLine(200);
    ImGui::Text("%u / %u", totalUsedFaces, totalMaxFaces);

    uint64_t faceUsedBytes = static_cast<uint64_t>(totalUsedFaces) * FACES_REGION_SIZE;
    uint64_t faceTotalBytes = static_cast<uint64_t>(totalMaxFaces) * FACES_REGION_SIZE;
    ImGui::Text("  Used/Total Memory:");
    ImGui::SameLine(200);
    ImGui::Text("%.2f MB / %.2f MB",
                static_cast<double>(faceUsedBytes) / (1024.0 * 1024.0),
                static_cast<double>(faceTotalBytes) / (1024.0 * 1024.0));

    ImGui::Spacing();
    ImGui::Separator();

    uint64_t totalUsedBytes = faceUsedBytes;
    uint64_t totalBytes = FACES_BUFFER_SIZE * buffers.size();
    ImGui::Text("Total Face Buffer Usage:");
    ImGui::SameLine(200);
    float overallUsage = totalBytes > 0 ? static_cast<float>(totalUsedBytes) / totalBytes : 0.0f;
    ImGui::ProgressBar(overallUsage, ImVec2(-1, 0), nullptr);
    ImGui::SameLine(0, 10);
    ImGui::Text("%.1f%%", overallUsage * 100.0f);
    ImGui::Text("  Total Memory:");
    ImGui::SameLine(200);
    ImGui::Text("%.2f MB / %.2f MB",
                static_cast<double>(totalUsedBytes) / (1024.0 * 1024.0),
                static_cast<double>(totalBytes) / (1024.0 * 1024.0));

    ImGui::Spacing();
    ImGui::Separator();

    ImGui::Text("Draw Commands:");
    ImGui::SameLine(200);
    ImGui::Text("%u", totalDrawCount);

    ImGui::Spacing();
    ImGui::TextDisabled("Memory Efficiency:");
    ImGui::Indent();
}

void VoxelBufferVisualizer::draw_fragmentation_info(VoxelTerrainRenderer* voxelRenderer) {
    const auto& buffers = voxelRenderer->get_voxel_buffers();

    ImGui::Text("Fragmentation Analysis");
    ImGui::Spacing();

    size_t totalFaceFragments = 0;
    uint32_t largestFaceBlock = 0;

    for (const auto& buffer : buffers) {
        totalFaceFragments += buffer.get_free_face_regions().size();
        largestFaceBlock = std::max(largestFaceBlock, buffer.get_largest_free_face_block());
    }

    ImGui::Text("Face Buffer:");
    ImGui::Text("  Free Fragments:");
    ImGui::SameLine(200);
    ImGui::Text("%zu", totalFaceFragments);

    ImGui::Text("  Largest Free Block:");
    ImGui::SameLine(200);
    uint64_t largestBytes = static_cast<uint64_t>(largestFaceBlock) * FACES_REGION_SIZE;
    if (largestBytes >= 1024 * 1024) {
        ImGui::Text("%u regions (%.2f MB)", largestFaceBlock,
                   static_cast<double>(largestBytes) / (1024.0 * 1024.0));
    } else {
        ImGui::Text("%u regions (%.2f KB)", largestFaceBlock,
                   static_cast<double>(largestBytes) / 1024.0);
    }

    ImGui::Spacing();
    ImGui::Separator();

    if (totalFaceFragments > 50) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ High fragmentation detected!");
        ImGui::Text("  Consider defragmentation if allocations are failing.");
    } else if (totalFaceFragments > 20) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "⚠ Moderate fragmentation");
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Low fragmentation");
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Allocation Capacity:");
    ImGui::Indent();
    uint32_t largestAllocQuads = largestFaceBlock * FACES_PER_REGION;
    ImGui::Text("Largest allocable chunk: %u faces (%u quads)",
                largestFaceBlock * FACES_PER_REGION, largestAllocQuads);
    ImGui::Unindent();
}