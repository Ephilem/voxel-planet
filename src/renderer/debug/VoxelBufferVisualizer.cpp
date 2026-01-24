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

        ImGui::Text("64MB Mesh Buffer Memory Map");
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

      float vertexSectionWidth = static_cast<float>(VERTEX_SECTION_SIZE) / TOTAL_BUFFER_SIZE * width;
      float indexSectionWidth = static_cast<float>(INDEX_SECTION_SIZE) / TOTAL_BUFFER_SIZE * width;

      for (size_t bufIdx = 0; bufIdx < buffers.size(); bufIdx++) {
          const auto& buffer = buffers[bufIdx];
          float yOffset = canvas_pos.y + bufIdx * bufferHeight;
          float sectionHeight = bufferHeight - spacing;

          char label[32];
          snprintf(label, sizeof(label), "Buffer %zu", bufIdx);
          draw_list->AddText(ImVec2(canvas_pos.x - 60, yOffset + sectionHeight * 0.3f),
                            IM_COL32(200, 200, 200, 255), label);

          float vxStart = canvas_pos.x;
          float vxEnd = canvas_pos.x + vertexSectionWidth;

          draw_list->AddRectFilled(
              ImVec2(vxStart, yOffset),
              ImVec2(vxEnd, yOffset + sectionHeight),
              IM_COL32(80, 140, 200, 255));

          const auto& freeVertexRegions = buffer.get_free_vertex_regions();
          for (const auto& [regionStart, regionCount] : freeVertexRegions) {
              float blockStart = vxStart + (static_cast<float>(regionStart) / MAX_VERTEX_REGION) * vertexSectionWidth;
              float blockWidth = (static_cast<float>(regionCount) / MAX_VERTEX_REGION) * vertexSectionWidth;

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
              ImVec2(vxStart, yOffset),
              ImVec2(vxEnd, yOffset + sectionHeight),
              IM_COL32(255, 255, 255, 128), 0.0f, 0, 1.0f);

          float ixStart = canvas_pos.x + vertexSectionWidth;
          float ixEnd = ixStart + indexSectionWidth;

          draw_list->AddRectFilled(
              ImVec2(ixStart, yOffset),
              ImVec2(ixEnd, yOffset + sectionHeight),
              IM_COL32(100, 180, 80, 255));

          const auto& freeIndexRegions = buffer.get_free_index_regions();
          for (const auto& [regionStart, regionCount] : freeIndexRegions) {
              float blockStart = ixStart + (static_cast<float>(regionStart) / MAX_INDEX_REGION) * indexSectionWidth;
              float blockWidth = (static_cast<float>(regionCount) / MAX_INDEX_REGION) * indexSectionWidth;

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
              ImVec2(ixStart, yOffset),
              ImVec2(ixEnd, yOffset + sectionHeight),
              IM_COL32(255, 255, 255, 128), 0.0f, 0, 1.0f);
      }

      draw_list->AddText(
          ImVec2(canvas_pos.x + vertexSectionWidth * 0.5f - 25, canvas_pos.y - 18),
          IM_COL32(255, 255, 255, 255), "VERTEX");
      draw_list->AddText(
          ImVec2(canvas_pos.x + vertexSectionWidth + indexSectionWidth * 0.5f - 20, canvas_pos.y - 18),
          IM_COL32(255, 255, 255, 255), "INDEX");

      ImGui::Dummy(ImVec2(width, height));

      ImGui::Spacing();
      ImGui::Text("Legend:");
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.31f, 0.55f, 0.78f, 1.0f), "■"); ImGui::SameLine();
      ImGui::Text("Vertex Used");
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.39f, 0.70f, 0.31f, 1.0f), "■"); ImGui::SameLine();
      ImGui::Text("Index Used");
      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.24f, 0.24f, 0.24f, 1.0f), "■"); ImGui::SameLine();
      ImGui::Text("Free (fragmentation)");
  }

void VoxelBufferVisualizer::draw_statistics(VoxelTerrainRenderer* voxelRenderer) {
    const auto& buffers = voxelRenderer->get_voxel_buffers();

    ImGui::Text("Memory Usage Statistics");
    ImGui::Spacing();

    uint32_t totalUsedVertex = 0;
    uint32_t totalUsedIndex = 0;
    uint32_t totalDrawCount = 0;

    for (const auto& buffer : buffers) {
        totalUsedVertex += buffer.get_used_vertex_regions();
        totalUsedIndex += buffer.get_used_index_regions();
        totalDrawCount += buffer.get_draw_count();
    }

    uint32_t totalMaxVertex = MAX_VERTEX_REGION * buffers.size();
    uint32_t totalMaxIndex = MAX_INDEX_REGION * buffers.size();

    ImGui::Text("Vertex Section:");
    ImGui::SameLine(200);
    float vertexUsage = totalMaxVertex > 0 ? static_cast<float>(totalUsedVertex) / totalMaxVertex : 0.0f;
    ImGui::ProgressBar(vertexUsage, ImVec2(-1, 0), nullptr);
    ImGui::SameLine(0, 10);
    ImGui::Text("%.1f%%", vertexUsage * 100.0f);

    ImGui::Text("  Used/Total Regions:");
    ImGui::SameLine(200);
    ImGui::Text("%u / %u", totalUsedVertex, totalMaxVertex);

    uint64_t vertexUsedBytes = static_cast<uint64_t>(totalUsedVertex) * VERTEX_REGION_SIZE;
    uint64_t vertexTotalBytes = static_cast<uint64_t>(totalMaxVertex) * VERTEX_REGION_SIZE;
    ImGui::Text("  Used/Total Memory:");
    ImGui::SameLine(200);
    ImGui::Text("%.2f MB / %.2f MB", static_cast<double>(vertexUsedBytes) / (1024.0 * 1024.0), static_cast<double>(vertexTotalBytes) / (1024.0 * 1024.0));

    ImGui::Spacing();

    ImGui::Text("Index Section:");
    ImGui::SameLine(200);
    float indexUsage = totalMaxIndex > 0 ? static_cast<float>(totalUsedIndex) / totalMaxIndex : 0.0f;
    ImGui::ProgressBar(indexUsage, ImVec2(-1, 0), nullptr);
    ImGui::SameLine(0, 10);
    ImGui::Text("%.1f%%", indexUsage * 100.0f);

    ImGui::Text("  Used/Total Regions:");
    ImGui::SameLine(200);
    ImGui::Text("%u / %u", totalUsedIndex, totalMaxIndex);

    uint64_t indexUsedBytes = static_cast<uint64_t>(totalUsedIndex) * INDEX_REGION_SIZE;
    uint64_t indexTotalBytes = static_cast<uint64_t>(totalMaxIndex) * INDEX_REGION_SIZE;
    ImGui::Text("  Used/Total Memory:");
    ImGui::SameLine(200);
    ImGui::Text("%.2f MB / %.2f MB", static_cast<double>(indexUsedBytes) / (1024.0 * 1024.0), static_cast<double>(indexTotalBytes) / (1024.0 * 1024.0));

    ImGui::Spacing();

    uint64_t totalUsedBytes = vertexUsedBytes + indexUsedBytes;
    uint64_t totalBytes = TOTAL_BUFFER_SIZE * buffers.size();
    ImGui::Separator();
    ImGui::Text("Total Mesh Buffer Usage:");
    ImGui::SameLine(200);
    float overallUsage = totalBytes > 0 ? static_cast<float>(totalUsedBytes) / totalBytes : 0.0f;
    ImGui::ProgressBar(overallUsage, ImVec2(-1, 0), nullptr);
    ImGui::SameLine(0, 10);
    ImGui::Text("%.1f%%", overallUsage * 100.0f);
    ImGui::Text("  Total Memory:");
    ImGui::SameLine(200);
    ImGui::Text("%.2f MB / %.2f MB", static_cast<double>(totalUsedBytes) / (1024.0 * 1024.0), static_cast<double>(totalBytes) / (1024.0 * 1024.0));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("Draw Commands:");
    ImGui::SameLine(200);
    ImGui::Text("%u", totalDrawCount);
}

void VoxelBufferVisualizer::draw_fragmentation_info(VoxelTerrainRenderer* voxelRenderer) {
    const auto& buffers = voxelRenderer->get_voxel_buffers();

    ImGui::Text("Fragmentation Analysis");
    ImGui::Spacing();

    size_t totalVertexFragments = 0;
    size_t totalIndexFragments = 0;

    uint32_t largestVertexBlock = 0;
    uint32_t largestIndexBlock = 0;

    for (const auto& buffer : buffers) {
        totalVertexFragments += buffer.get_free_vertex_regions().size();
        totalIndexFragments += buffer.get_free_index_regions().size();

        largestVertexBlock = std::max(largestVertexBlock, buffer.get_largest_free_vertex_block());
        largestIndexBlock = std::max(largestIndexBlock, buffer.get_largest_free_index_block());
    }

    ImGui::Text("Vertex Section:");
    ImGui::Text("  Free Fragments:");
    ImGui::SameLine(200);
    ImGui::Text("%zu", totalVertexFragments);
    ImGui::Text("  Largest Free Block:");
    ImGui::SameLine(200);
    ImGui::Text("%u regions (%.2f KB)", largestVertexBlock, static_cast<float>(largestVertexBlock * VERTEX_REGION_SIZE) / 1024.0f);

    ImGui::Spacing();

    ImGui::Text("Index Section:");
    ImGui::Text("  Free Fragments:");
    ImGui::SameLine(200);
    ImGui::Text("%zu", totalIndexFragments);
    ImGui::Text("  Largest Free Block:");
    ImGui::SameLine(200);
    ImGui::Text("%u regions (%.2f KB)", largestIndexBlock, static_cast<float>(largestIndexBlock * INDEX_REGION_SIZE) / 1024.0f);

    ImGui::Spacing();
    ImGui::Separator();

    size_t totalFragments = totalVertexFragments + totalIndexFragments;
    if (totalFragments > 50) {
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "⚠ High fragmentation detected!");
        ImGui::Text("  Consider defragmentation if allocations are failing.");
    } else if (totalFragments > 20) {
        ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "⚠ Moderate fragmentation");
    } else {
        ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "✓ Low fragmentation");
    }
}
