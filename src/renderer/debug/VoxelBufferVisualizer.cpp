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

        // Draw the visual memory map
        ImGui::Text("64MB Buffer Memory Map (Combined View)");
        ImGui::Spacing();

        float availWidth = ImGui::GetContentRegionAvail().x;
        draw_memory_map(voxelRenderer, availWidth, 120.0f);

        ImGui::Spacing();
        ImGui::Separator();

        // Draw statistics
        draw_statistics(voxelRenderer);

        ImGui::Spacing();
        ImGui::Separator();

        // Draw fragmentation info
        draw_fragmentation_info(voxelRenderer);
    }
    ImGui::End();
}

void VoxelBufferVisualizer::draw_memory_map(VoxelTerrainRenderer* voxelRenderer, float width, float height) {
    const auto& buffers = voxelRenderer->get_voxel_buffers();

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size(width, height);

    // Background
    draw_list->AddRectFilled(canvas_pos,
        ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
        IM_COL32(40, 40, 40, 255));

    // Calculate section widths based on their actual sizes
    float vertexSectionWidth = (float)VERTEX_SECTION_SIZE / TOTAL_BUFFER_SIZE * width;
    float indexSectionWidth = (float)INDEX_SECTION_SIZE / TOTAL_BUFFER_SIZE * width;
    float indirectSectionWidth = (float)INDIRECT_SECTION_SIZE / TOTAL_BUFFER_SIZE * width;

    float currentX = canvas_pos.x;

    // Draw each section for all buffers combined
    // We'll use different shades to show used vs free space

    // === VERTEX SECTION ===
    {
        ImVec2 sectionMin(currentX, canvas_pos.y);
        ImVec2 sectionMax(currentX + vertexSectionWidth, canvas_pos.y + canvas_size.y);

        // Draw section background (base color for vertex)
        draw_list->AddRectFilled(sectionMin, sectionMax, IM_COL32(50, 100, 150, 255));

        // Draw allocated regions as darker blocks
        for (const auto& buffer : buffers) {
            uint32_t usedRegions = buffer.get_used_vertex_regions();
            if (usedRegions > 0) {
                float usedPercentage = (float)usedRegions / MAX_VERTEX_REGION;
                float usedWidth = vertexSectionWidth * usedPercentage;

                draw_list->AddRectFilled(
                    ImVec2(currentX, canvas_pos.y),
                    ImVec2(currentX + usedWidth, canvas_pos.y + canvas_size.y),
                    IM_COL32(100, 180, 255, 255));
            }
        }

        // Draw section border
        draw_list->AddRect(sectionMin, sectionMax, IM_COL32(255, 255, 255, 128), 0.0f, 0, 2.0f);

        // Label
        ImVec2 labelPos(currentX + vertexSectionWidth * 0.5f - 30, canvas_pos.y + canvas_size.y * 0.5f);
        draw_list->AddText(labelPos, IM_COL32(255, 255, 255, 255), "VERTEX");

        currentX += vertexSectionWidth;
    }

    // === INDEX SECTION ===
    {
        ImVec2 sectionMin(currentX, canvas_pos.y);
        ImVec2 sectionMax(currentX + indexSectionWidth, canvas_pos.y + canvas_size.y);

        // Draw section background (base color for index)
        draw_list->AddRectFilled(sectionMin, sectionMax, IM_COL32(100, 150, 50, 255));

        // Draw allocated regions
        for (const auto& buffer : buffers) {
            uint32_t usedRegions = buffer.get_used_index_regions();
            if (usedRegions > 0) {
                float usedPercentage = (float)usedRegions / MAX_INDEX_REGION;
                float usedWidth = indexSectionWidth * usedPercentage;

                draw_list->AddRectFilled(
                    ImVec2(currentX, canvas_pos.y),
                    ImVec2(currentX + usedWidth, canvas_pos.y + canvas_size.y),
                    IM_COL32(180, 255, 100, 255));
            }
        }

        // Draw section border
        draw_list->AddRect(sectionMin, sectionMax, IM_COL32(255, 255, 255, 128), 0.0f, 0, 2.0f);

        // Label
        ImVec2 labelPos(currentX + indexSectionWidth * 0.5f - 20, canvas_pos.y + canvas_size.y * 0.5f);
        draw_list->AddText(labelPos, IM_COL32(255, 255, 255, 255), "INDEX");

        currentX += indexSectionWidth;
    }

    // === INDIRECT SECTION ===
    {
        ImVec2 sectionMin(currentX, canvas_pos.y);
        ImVec2 sectionMax(currentX + indirectSectionWidth, canvas_pos.y + canvas_size.y);

        // Draw section background (base color for indirect)
        draw_list->AddRectFilled(sectionMin, sectionMax, IM_COL32(150, 50, 100, 255));

        // Draw allocated regions
        for (const auto& buffer : buffers) {
            uint32_t usedRegions = buffer.get_used_indirect_regions();
            if (usedRegions > 0) {
                float usedPercentage = (float)usedRegions / MAX_INDIRECT_REGION;
                float usedWidth = indirectSectionWidth * usedPercentage;

                draw_list->AddRectFilled(
                    ImVec2(currentX, canvas_pos.y),
                    ImVec2(currentX + usedWidth, canvas_pos.y + canvas_size.y),
                    IM_COL32(255, 100, 180, 255));
            }
        }

        // Draw section border
        draw_list->AddRect(sectionMin, sectionMax, IM_COL32(255, 255, 255, 128), 0.0f, 0, 2.0f);

        // Label
        ImVec2 labelPos(currentX + indirectSectionWidth * 0.5f - 30, canvas_pos.y + canvas_size.y * 0.5f);
        draw_list->AddText(labelPos, IM_COL32(255, 255, 255, 255), "INDIRECT");
    }

    // Reserve space for the canvas
    ImGui::Dummy(canvas_size);

    // Legend
    ImGui::Spacing();
    ImGui::Text("Legend:");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.2f, 0.4f, 0.6f, 1.0f), "■"); ImGui::SameLine();
    ImGui::Text("Free");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "■"); ImGui::SameLine();
    ImGui::Text("Allocated");
}

void VoxelBufferVisualizer::draw_statistics(VoxelTerrainRenderer* voxelRenderer) {
    const auto& buffers = voxelRenderer->get_voxel_buffers();

    ImGui::Text("Memory Usage Statistics");
    ImGui::Spacing();

    // Aggregate stats across all buffers
    uint32_t totalUsedVertex = 0;
    uint32_t totalUsedIndex = 0;
    uint32_t totalUsedIndirect = 0;

    for (const auto& buffer : buffers) {
        totalUsedVertex += buffer.get_used_vertex_regions();
        totalUsedIndex += buffer.get_used_index_regions();
        totalUsedIndirect += buffer.get_used_indirect_regions();
    }

    uint32_t totalMaxVertex = MAX_VERTEX_REGION * buffers.size();
    uint32_t totalMaxIndex = MAX_INDEX_REGION * buffers.size();
    uint64_t totalMaxIndirect = MAX_INDIRECT_REGION * buffers.size();

    // Vertex section
    ImGui::Text("Vertex Section:");
    ImGui::SameLine(200);
    float vertexUsage = totalMaxVertex > 0 ? (float)totalUsedVertex / totalMaxVertex : 0.0f;
    ImGui::ProgressBar(vertexUsage, ImVec2(-1, 0), nullptr);
    ImGui::SameLine(0, 10);
    ImGui::Text("%.1f%%", vertexUsage * 100.0f);

    ImGui::Text("  Used/Total Regions:");
    ImGui::SameLine(200);
    ImGui::Text("%u / %u", totalUsedVertex, totalMaxVertex);

    uint64_t vertexUsedBytes = (uint64_t)totalUsedVertex * VERTEX_REGION_SIZE;
    uint64_t vertexTotalBytes = (uint64_t)totalMaxVertex * VERTEX_REGION_SIZE;
    ImGui::Text("  Used/Total Memory:");
    ImGui::SameLine(200);
    ImGui::Text("%.2f MB / %.2f MB", vertexUsedBytes / (1024.0 * 1024.0), vertexTotalBytes / (1024.0 * 1024.0));

    ImGui::Spacing();

    // Index section
    ImGui::Text("Index Section:");
    ImGui::SameLine(200);
    float indexUsage = totalMaxIndex > 0 ? (float)totalUsedIndex / totalMaxIndex : 0.0f;
    ImGui::ProgressBar(indexUsage, ImVec2(-1, 0), nullptr);
    ImGui::SameLine(0, 10);
    ImGui::Text("%.1f%%", indexUsage * 100.0f);

    ImGui::Text("  Used/Total Regions:");
    ImGui::SameLine(200);
    ImGui::Text("%u / %u", totalUsedIndex, totalMaxIndex);

    uint64_t indexUsedBytes = (uint64_t)totalUsedIndex * INDEX_REGION_SIZE;
    uint64_t indexTotalBytes = (uint64_t)totalMaxIndex * INDEX_REGION_SIZE;
    ImGui::Text("  Used/Total Memory:");
    ImGui::SameLine(200);
    ImGui::Text("%.2f MB / %.2f MB", indexUsedBytes / (1024.0 * 1024.0), indexTotalBytes / (1024.0 * 1024.0));

    ImGui::Spacing();

    // Indirect section
    ImGui::Text("Indirect Section:");
    ImGui::SameLine(200);
    float indirectUsage = totalMaxIndirect > 0 ? (float)totalUsedIndirect / totalMaxIndirect : 0.0f;
    ImGui::ProgressBar(indirectUsage, ImVec2(-1, 0), nullptr);
    ImGui::SameLine(0, 10);
    ImGui::Text("%.1f%%", indirectUsage * 100.0f);

    ImGui::Text("  Used/Total Regions:");
    ImGui::SameLine(200);
    ImGui::Text("%u / %llu", totalUsedIndirect, totalMaxIndirect);

    uint64_t indirectUsedBytes = (uint64_t)totalUsedIndirect * INDIRECT_REGION_SIZE;
    uint64_t indirectTotalBytes = (uint64_t)totalMaxIndirect * INDIRECT_REGION_SIZE;
    ImGui::Text("  Used/Total Memory:");
    ImGui::SameLine(200);
    ImGui::Text("%.2f MB / %.2f MB", indirectUsedBytes / (1024.0 * 1024.0), indirectTotalBytes / (1024.0 * 1024.0));

    ImGui::Spacing();

    // Overall usage
    uint64_t totalUsedBytes = vertexUsedBytes + indexUsedBytes + indirectUsedBytes;
    uint64_t totalBytes = TOTAL_BUFFER_SIZE * buffers.size();
    ImGui::Separator();
    ImGui::Text("Total Buffer Usage:");
    ImGui::SameLine(200);
    float overallUsage = totalBytes > 0 ? (float)totalUsedBytes / totalBytes : 0.0f;
    ImGui::ProgressBar(overallUsage, ImVec2(-1, 0), nullptr);
    ImGui::SameLine(0, 10);
    ImGui::Text("%.1f%%", overallUsage * 100.0f);
    ImGui::Text("  Total Memory:");
    ImGui::SameLine(200);
    ImGui::Text("%.2f MB / %.2f MB", totalUsedBytes / (1024.0 * 1024.0), totalBytes / (1024.0 * 1024.0));
}

void VoxelBufferVisualizer::draw_fragmentation_info(VoxelTerrainRenderer* voxelRenderer) {
    const auto& buffers = voxelRenderer->get_voxel_buffers();

    ImGui::Text("Fragmentation Analysis");
    ImGui::Spacing();

    // Aggregate fragmentation info across all buffers
    size_t totalVertexFragments = 0;
    size_t totalIndexFragments = 0;
    size_t totalIndirectFragments = 0;

    uint32_t largestVertexBlock = 0;
    uint32_t largestIndexBlock = 0;
    uint32_t largestIndirectBlock = 0;

    for (const auto& buffer : buffers) {
        totalVertexFragments += buffer.get_free_vertex_regions().size();
        totalIndexFragments += buffer.get_free_index_regions().size();
        totalIndirectFragments += buffer.get_free_indirect_regions().size();

        largestVertexBlock = std::max(largestVertexBlock, buffer.get_largest_free_vertex_block());
        largestIndexBlock = std::max(largestIndexBlock, buffer.get_largest_free_index_block());
        largestIndirectBlock = std::max(largestIndirectBlock, buffer.get_largest_free_indirect_block());
    }

    // Vertex fragmentation
    ImGui::Text("Vertex Section:");
    ImGui::Text("  Free Fragments:");
    ImGui::SameLine(200);
    ImGui::Text("%zu", totalVertexFragments);
    ImGui::Text("  Largest Free Block:");
    ImGui::SameLine(200);
    ImGui::Text("%u regions (%.2f KB)", largestVertexBlock, (largestVertexBlock * VERTEX_REGION_SIZE) / 1024.0f);

    ImGui::Spacing();

    // Index fragmentation
    ImGui::Text("Index Section:");
    ImGui::Text("  Free Fragments:");
    ImGui::SameLine(200);
    ImGui::Text("%zu", totalIndexFragments);
    ImGui::Text("  Largest Free Block:");
    ImGui::SameLine(200);
    ImGui::Text("%u regions (%.2f KB)", largestIndexBlock, (largestIndexBlock * INDEX_REGION_SIZE) / 1024.0f);

    ImGui::Spacing();

    // Indirect fragmentation
    ImGui::Text("Indirect Section:");
    ImGui::Text("  Free Fragments:");
    ImGui::SameLine(200);
    ImGui::Text("%zu", totalIndirectFragments);
    ImGui::Text("  Largest Free Block:");
    ImGui::SameLine(200);
    ImGui::Text("%u regions (%.2f bytes)", largestIndirectBlock, largestIndirectBlock * INDIRECT_REGION_SIZE);

    ImGui::Spacing();
    ImGui::Separator();

    // Fragmentation warning
    size_t totalFragments = totalVertexFragments + totalIndexFragments + totalIndirectFragments;
    if (totalFragments > 50) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ High fragmentation detected!");
        ImGui::Text("  Consider defragmentation if allocations are failing.");
    } else if (totalFragments > 20) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "⚠ Moderate fragmentation");
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Low fragmentation");
    }
}
