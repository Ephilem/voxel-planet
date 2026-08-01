#include "VoxelBufferDebugger.h"

#include <algorithm>
#include <cfloat>
#include <cstdio>

#include <imgui.h>

using namespace vp;

namespace {
    /// Turn a byte count into something readable, since the arena is sized in hundreds of MB
    void format_bytes(char *out, size_t outSize, uint64_t bytes) {
        constexpr double MB = 1024.0 * 1024.0;
        snprintf(out, outSize, "%.1f MB", static_cast<double>(bytes) / MB);
    }

    /// Green when there is room, red when there is not
    ImU32 occupancy_color(float fraction) {
        const float r = 0.15f + 0.75f * fraction;
        const float g = 0.75f - 0.60f * fraction;
        return ImGui::GetColorU32(ImVec4(r, g, 0.25f, 1.0f));
    }
}

void VoxelBufferDebugger::rebuild_region_map(const VoxelBuffer &buffer) {
    m_regionUsed.assign(MAX_FACES_REGIONS, 1);

    // The arena only tracks what is free, so everything else is in use by definition
    for (const auto &[start, count]: buffer.get_free_face_regions()) {
        const uint32_t end = std::min(start + count, MAX_FACES_REGIONS);
        for (uint32_t i = start; i < end; ++i) m_regionUsed[i] = 0;
    }
}

void VoxelBufferDebugger::draw_occupancy_strip(const char *label) {
    m_occupancy.assign(OCCUPANCY_BUCKETS, 0.0f);

    const float perBucket = static_cast<float>(MAX_FACES_REGIONS) / static_cast<float>(OCCUPANCY_BUCKETS);

    for (uint32_t region = 0; region < MAX_FACES_REGIONS; ++region) {
        if (!m_regionUsed[region]) continue;
        const int bucket = std::min(static_cast<int>(static_cast<float>(region) / perBucket), OCCUPANCY_BUCKETS - 1);
        m_occupancy[bucket] += 1.0f;
    }

    ImGui::TextUnformatted(label);

    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    constexpr float height = 24.0f;

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(origin, ImVec2(origin.x + width, origin.y + height),
                            ImGui::GetColorU32(ImVec4(0.08f, 0.08f, 0.10f, 1.0f)));

    const float bucketWidth = width / static_cast<float>(OCCUPANCY_BUCKETS);

    for (int i = 0; i < OCCUPANCY_BUCKETS; ++i) {
        const float fraction = std::min(1.0f, m_occupancy[i] / perBucket);
        if (fraction <= 0.0f) continue;

        // Anything non empty gets at least a sliver, or a lone allocation in a wide bucket
        // would be invisible exactly when it matters
        const float barHeight = std::max(2.0f, height * fraction);
        const float x = origin.x + static_cast<float>(i) * bucketWidth;

        drawList->AddRectFilled(ImVec2(x, origin.y + height - barHeight),
                                ImVec2(x + bucketWidth + 1.0f, origin.y + height),
                                occupancy_color(fraction));
    }

    ImGui::Dummy(ImVec2(width, height));

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Face region space, low addresses on the left.\n"
                          "Gaps between filled areas are fragmentation.");
    }
}

void VoxelBufferDebugger::draw(const char *title,
                               const VoxelBuffer &buffer,
                               std::span<const VoxelChunkMesh> meshes,
                               std::span<const PlanetNodeCoord> coords) {
    if (!ImGui::Begin(title)) {
        ImGui::End();
        return;
    }

    rebuild_region_map(buffer);

    // --- Face regions ---

    const uint32_t usedRegions = buffer.get_used_face_regions();
    const uint32_t largestFree = buffer.get_largest_free_face_block();
    const auto &freeList = buffer.get_free_face_regions();

    uint32_t totalFree = 0;
    for (const auto &[start, count]: freeList) totalFree += count;

    char usedBytes[32];
    char totalBytes[32];
    format_bytes(usedBytes, sizeof(usedBytes), static_cast<uint64_t>(usedRegions) * FACES_REGION_SIZE);
    format_bytes(totalBytes, sizeof(totalBytes), FACES_BUFFER_SIZE);

    const float usedFraction = static_cast<float>(usedRegions) / static_cast<float>(MAX_FACES_REGIONS);

    ImGui::Text("Face regions  %u / %u   (%s / %s)", usedRegions, MAX_FACES_REGIONS, usedBytes, totalBytes);
    ImGui::ProgressBar(usedFraction, ImVec2(-1.0f, 0.0f));

    draw_occupancy_strip("Region map");

    // A single long run is healthy. Many short ones mean a large allocation can fail while the
    // arena still reports plenty of free space.
    const float fragmentation = totalFree > 0
                                    ? 1.0f - static_cast<float>(largestFree) / static_cast<float>(totalFree)
                                    : 0.0f;

    ImGui::Text("Free blocks: %zu     largest run: %u regions", freeList.size(), largestFree);
    ImGui::Text("Fragmentation: %.1f %%", fragmentation * 100.0f);
    if (fragmentation > 0.5f) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.2f, 1.0f), "a large mesh may fail to fit");
    }

    ImGui::Separator();

    // --- Draw slots ---

    const uint32_t slotHighWater = buffer.get_unculled_draw_count();
    const auto freeSlots = static_cast<uint32_t>(buffer.get_free_draw_slots().size());

    ImGui::Text("Draw slots    %u live, %u recycled, high water %u",
                slotHighWater - freeSlots, freeSlots, slotHighWater);

    ImGui::Separator();

    // --- Allocations ---

    m_allocations.clear();
    uint64_t totalFaces = 0;

    for (size_t slot = 0; slot < meshes.size(); ++slot) {
        const VoxelChunkMesh &mesh = meshes[slot];
        if (!mesh.is_allocated()) continue;

        SlotInfo info{};
        info.drawSlot = static_cast<uint32_t>(slot);
        info.faceCount = static_cast<uint32_t>(mesh.faceCount);
        info.faceRegionStart = mesh.faceRegionStart;
        info.faceRegionCount = mesh.faceRegionCount;
        if (slot < coords.size()) info.coord = coords[slot];

        totalFaces += info.faceCount;
        m_allocations.push_back(info);
    }

    // Each mesh rounds up to a whole region, so a chunk of 1010 faces reserves 2000 slots. Past
    // a few percent this is the cheapest capacity to win back, by resizing FACES_PER_REGION.
    const uint64_t reservedFaces = static_cast<uint64_t>(usedRegions) * FACES_PER_REGION;
    const double waste = reservedFaces > 0
                             ? 100.0 * (1.0 - static_cast<double>(totalFaces) / static_cast<double>(reservedFaces))
                             : 0.0;

    ImGui::Text("Faces stored  %llu", static_cast<unsigned long long>(totalFaces));
    ImGui::Text("Reserved      %llu   (%.1f %% lost to region rounding)",
                static_cast<unsigned long long>(reservedFaces), waste);

    if (!m_allocations.empty()) {
        ImGui::Text("Mean faces per mesh: %llu",
                    static_cast<unsigned long long>(totalFaces / m_allocations.size()));
    }

    // --- Face count histogram ---

    if (!m_allocations.empty()) {
        uint32_t maxFaces = 1;
        for (const SlotInfo &info: m_allocations) maxFaces = std::max(maxFaces, info.faceCount);

        m_histogram.assign(HISTOGRAM_BUCKETS, 0.0f);
        for (const SlotInfo &info: m_allocations) {
            const int bucket = std::min(
                static_cast<int>(static_cast<uint64_t>(info.faceCount) * HISTOGRAM_BUCKETS / (maxFaces + 1)),
                HISTOGRAM_BUCKETS - 1);
            m_histogram[bucket] += 1.0f;
        }

        char overlay[64];
        snprintf(overlay, sizeof(overlay), "0 .. %u faces", maxFaces);
        ImGui::PlotHistogram("##faceHistogram", m_histogram.data(), HISTOGRAM_BUCKETS,
                             0, overlay, 0.0f, FLT_MAX, ImVec2(-1.0f, 60.0f));
    }

    ImGui::End();
}
