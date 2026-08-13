#include "PlanetLodPanel.h"

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>

#include "client/world/planet/planet_client_components.h"
#include "core/TracyIntegration.h"
#include "core/world/planet/planet_transform.h"
#include "imgui.h"
#include "renderer/world/planet/planet_rendering_components.h"
#include "renderer/world/planet/PlanetTileAtlas.h"
#include "renderer/world/planet/PlanetTileRenderer.h"

using namespace vp;

namespace {
constexpr float VALUE_COLUMN = 190.f;

const ImVec4 COLOR_OK{0.2f, 0.9f, 0.3f, 1.f};
const ImVec4 COLOR_WARN{1.0f, 0.6f, 0.1f, 1.f};
const ImVec4 COLOR_BAD{1.0f, 0.3f, 0.25f, 1.f};

void stat_row(const char* label, uint32_t value) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(VALUE_COLUMN);
    ImGui::Text("%u", value);
}

/// Same as stat_row, but the value turns orange past warnAt and red past badAt
void stat_row_threshold(const char* label, uint32_t value, uint32_t warnAt, uint32_t badAt) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(VALUE_COLUMN);
    if (value >= badAt) {
        ImGui::TextColored(COLOR_BAD, "%u", value);
    } else if (value >= warnAt) {
        ImGui::TextColored(COLOR_WARN, "%u", value);
    } else {
        ImGui::TextColored(COLOR_OK, "%u", value);
    }
}

void stat_row_fmt(const char* label, const char* fmt, ...) IM_FMTARGS(2);

void stat_row_fmt(const char* label, const char* fmt, ...) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(VALUE_COLUMN);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

void tooltip(const char* text) {
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", text);
}

/// Ratio bar labelled "value / total (pct%)", clamped so a zero total stays readable
void ratio_bar(const char* label, uint32_t value, uint32_t total, const ImVec4& color) {
    const float ratio = total > 0 ? float(value) / float(total) : 0.f;

    char overlay[64];
    snprintf(overlay, sizeof(overlay), "%u / %u  (%.1f%%)", value, total, ratio * 100.f);

    ImGui::TextUnformatted(label);
    ImGui::SameLine(VALUE_COLUMN);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, color);
    ImGui::ProgressBar(ratio, ImVec2(-1.f, 0.f), overlay);
    ImGui::PopStyleColor();
}

/// Signed delta, coloured by direction and hidden when nothing moved
void delta_row(const char* label, int64_t delta) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(VALUE_COLUMN);
    if (delta == 0) {
        ImGui::TextDisabled("0");
    } else {
        ImGui::TextColored(delta > 0 ? COLOR_WARN : COLOR_OK, "%+lld", static_cast<long long>(delta));
    }
}

/**
 * Plots a ring buffer in chronological order.
 *
 * ImGui walks the array linearly, so the write cursor is handed over as an offset to keep
 * the oldest sample on the left instead of a discontinuity wandering through the plot
 */
void history_plot(const char* label, const float* values, int count, int offset, float scaleMax, const ImVec4& color,
                  const char* overlay, float height) {
    ImGui::PushStyleColor(ImGuiCol_PlotLines, color);
    ImGui::PlotLines(label, values, count, offset, overlay, 0.f, scaleMax, ImVec2(-1.f, height));
    ImGui::PopStyleColor();
}

float array_max(const float* values, int count) {
    float best = 0.f;
    for (int i = 0; i < count; ++i)
        best = std::max(best, values[i]);
    return best;
}
} // namespace

void PlanetLodPanel::render(flecs::world& ecs) {
    VOXEL_ZONE_N("PlanetLodPanel-Display");

    bool found = false;
    uint32_t leafTotal = 0;

    ecs.each([&](flecs::entity e, PlanetTileLodComp& lod) {
        found = true;
        if (!lod.quadtree)
            return;

        auto& params = lod.params;
        const auto& stats = lod.quadtree->stats();
        leafTotal += stats.leafCount;

        // ---- Visualisation -------------------------------------------------
        if (ImGui::CollapsingHeader("Visualisation", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Draw node outlines", &lod.debugDrawNodes);

            ImGui::BeginDisabled(!lod.debugDrawNodes);
            int mode = int(lod.debugMode);
            if (ImGui::RadioButton("By level", &mode, int(PlanetQuadtrees::DebugMode::Level))) {
                lod.debugMode = PlanetQuadtrees::DebugMode(mode);
            }
            ImGui::SameLine();
            if (ImGui::RadioButton("By face", &mode, int(PlanetQuadtrees::DebugMode::Face))) {
                lod.debugMode = PlanetQuadtrees::DebugMode(mode);
            }

            ImGui::SliderInt("Segments / edge", &lod.debugSegmentsPerEdge, 1, 16);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Subdivisions per node edge. More segments make the\n"
                                  "outline follow the curvature instead of cutting through it");
            }
            ImGui::EndDisabled();

            bool frozen = lod.quadtree->is_frozen();
            if (ImGui::Checkbox("Freeze tree", &frozen)) {
                lod.quadtree->set_frozen(frozen);
            }
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Stop splitting and merging so the camera can fly\n"
                                  "around the current subdivision and inspect it");
            }
        }

        // ---- Tuning --------------------------------------------------------
        if (ImGui::CollapsingHeader("Tuning", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragScalar("Split factor", ImGuiDataType_Double, &params.splitFactor, 0.02f, nullptr, nullptr,
                              "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("A node splits while distance < factor * node size.\n"
                                  "Higher means more detail and more nodes");
            }

            ImGui::DragScalar("Merge hysteresis", ImGuiDataType_Double, &params.mergeHysteresis, 0.01f, nullptr,
                              nullptr, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Merge beyond splitFactor * hysteresis.\n"
                                  "1.0 makes a hovering camera split and merge every frame");
            }

            ImGui::DragScalar("Face Cull Angle Deg", ImGuiDataType_Double, &params.faceCullAngleDeg, 0.5f, nullptr,
                              nullptr, "%.1f deg");

            int maxLevel = params.maxLevel;
            if (ImGui::SliderInt("Max level", &maxLevel, 0, 20)) {
                params.maxLevel = uint8_t(maxLevel);
            }

            ImGui::Separator();
            ImGui::Text("Radius       %.2f m", params.planetRadius);
            ImGui::Text("Face size    %.2f m", planet_face_size(params.planetRadius));
            ImGui::Text("Leaf size    %.2f m", planet_node_size(params.planetRadius, params.maxLevel));

            // A leaf only tiles into whole chunks when this lands on a round number,
            // so it is the value worth watching while tuning the radius
            const double voxel = planet_voxel_size(params.planetRadius, params.maxLevel, params.chunkSize);
            const bool aligned = planet_is_voxel_aligned(params.planetRadius, params.maxLevel, params.chunkSize);

            ImGui::Text("Voxel size   ");
            ImGui::SameLine(0.f, 0.f);
            ImGui::TextColored(aligned ? ImVec4(0.2f, 0.9f, 0.3f, 1.f) : ImVec4(1.0f, 0.6f, 0.1f, 1.f), "%.4f m",
                               voxel);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Leaf arc / %u. The tangent warp keeps this uniform\n"
                                  "across the face. Green when whole to the millimeter",
                                  params.chunkSize);
            }

            if (!aligned) {
                ImGui::TextDisabled("nearest aligned radius: %.2f m",
                                    planet_snap_radius(params.planetRadius, params.maxLevel, params.chunkSize));
            }
        }

        // ---- Live stats ----------------------------------------------------
        if (ImGui::CollapsingHeader("Statistics", ImGuiTreeNodeFlags_DefaultOpen)) {
            stat_row("Live nodes", uint32_t(lod.quadtree->live_node_count()));
            stat_row("Leaves", stats.leafCount);
            ImGui::Separator();

            stat_row("Splits this frame", stats.splits);
            stat_row("Merges this frame", stats.merges);
            stat_row("Culled faces", stats.culledFaces);
            stat_row("Culled Nodes", stats.culledNodes);
            stat_row("Frustum Culled nodes", stats.frustumCulledNodes);
            stat_row("Balance splits", stats.balanceSplits);
            stat_row("Collected Tiles to render", stats.collectedTiles);
        }

        // ---- Tile generation (CPU side) ------------------------------------
        // Queue depth is the thing to watch: a pending count that never drains means the tree is
        // asking for tiles faster than the workers retire them, and every one of those nodes is
        // drawing from an ancestor in the meantime
        if (ImGui::CollapsingHeader("Tile generation", ImGuiTreeNodeFlags_DefaultOpen)) {
            const auto* stream = e.get<PlanetTileStreamComp>();

            if (!stream || !stream->generator) {
                ImGui::TextDisabled("No generator on this planet yet");
            } else {
                const PlanetTileGenerator& gen = *stream->generator;
                const auto& gs = gen.stats();

                stat_row("Worker threads", uint32_t(gen.worker_count()));
                ImGui::Separator();

                stat_row_threshold("Pending (queued)", uint32_t(gen.pending()), 64, 256);
                tooltip("Requested tiles not yet handed to the workers.\n"
                        "submit_pending() releases at most 16 per frame, deepest levels first");

                stat_row_threshold("In flight", uint32_t(gen.in_flight()), 128, 512);
                tooltip("Handed to a worker, not drained back yet");

                ImGui::Separator();
                stat_row("Requested / frame", gs.requestedThisFrame);
                stat_row("Submitted / frame", gs.submittedThisFrame);
                stat_row("Completed / frame", gs.completedThisFrame);

                ImGui::Separator();
                stat_row("Requested total", gs.requested);
                stat_row("Submitted total", gs.submitted);
                stat_row("Completed total", gs.completed);

                // Anything submitted but never completed is either in flight or lost
                const uint32_t outstanding = gs.submitted - gs.completed;
                stat_row_threshold("Outstanding", outstanding, 128, 512);
                tooltip("submitted - completed. Should track 'In flight' closely.\n"
                        "A gap that only grows means results are never drained");

                stat_row("Dedup rejects", gs.dedupRejects);
                tooltip("request() calls dropped because the tile was already\n"
                        "pending or in flight. Large is normal: the renderer\n"
                        "re-requests every unresolved tile each frame");

                ImGui::Separator();
                stat_row("Peak pending", gs.peakPending);
                stat_row("Peak in flight", gs.peakInFlight);
            }
        }
    });

    if (!found) {
        ImGui::TextDisabled("No planet with a PlanetTileLodComp in the world");
    }

    m_lastLeafCount = leafTotal;
    render_atlas_section(ecs);
}

void PlanetLodPanel::render_atlas_section(flecs::world& ecs) {
    const auto* ref = ecs.get<PlanetTileAtlasRef>();
    if (!ref || !ref->atlas) {
        ImGui::TextDisabled("Tile atlas not published (renderer module not initialised)");
        return;
    }

    // ---- Atlas residency ---------------------------------------------------
    if (ImGui::CollapsingHeader("Tile atlas", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& as = ref->atlas->stats();

        ratio_bar("Residency", as.resident, as.capacity, as.resident >= as.capacity ? COLOR_BAD : COLOR_OK);
        tooltip("Slices holding a tile. At 100% every new upload has to evict,\n"
                "and a tile used this frame can no longer be evicted, so uploads\n"
                "start failing instead");

        const size_t sliceBytes =
            size_t(PLANET_TILE_ATLAS_RESOLUTION) * PLANET_TILE_ATLAS_RESOLUTION * sizeof(uint16_t);
        stat_row_fmt("VRAM resident", "%.1f MiB of %.1f MiB", double(as.resident * sliceBytes) / (1024.0 * 1024.0),
                     double(as.capacity * sliceBytes) / (1024.0 * 1024.0));

        ImGui::Separator();
        stat_row("Uploads total", as.uploads);
        stat_row("Evictions total", as.evictions);
        stat_row("Expired total", as.expired);

        stat_row_threshold("Failed uploads", as.failedUploads, 1, 32);
        tooltip("Atlas full for the frame, or malformed tile data.\n"
                "Any non-zero value means heightmaps were dropped on the floor");

        // An eviction rate close to the upload rate means the working set does not fit: tiles are
        // uploaded, evicted, and regenerated in a loop that never converges
        if (as.uploads > 0) {
            const float churn = float(as.evictions) / float(as.uploads);
            ImGui::TextUnformatted("Churn (evict/upload)");
            ImGui::SameLine(VALUE_COLUMN);
            ImGui::TextColored(churn > 0.5f ? COLOR_BAD : churn > 0.2f ? COLOR_WARN : COLOR_OK, "%.2f", churn);
            tooltip("Evictions per upload. Above ~0.5 the atlas is thrashing:\n"
                    "raise PLANET_TILE_ATLAS_SIZE or lower the max level");
        }
    }

    // ---- Slot resolution ---------------------------------------------------
    // This is the CPU-side proof that uploads actually reach the shader: a tile can be generated,
    // uploaded and resident, and still draw from an ancestor if resolve_atlas_slot missed it
    if (!ref->renderer)
        return;

    if (ImGui::CollapsingHeader("Atlas slot resolution", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto& rs = ref->renderer->stats();
        const auto& as = ref->atlas->stats();
        const uint32_t total = rs.exactSlots + rs.fallbackSlots + rs.missingSlots;

        // The panel runs in PostUpdate, render_planets in OnStore, so these are last frame's
        ImGui::TextDisabled("(previous frame)");

        stat_row("Instances drawn", ref->renderer->last_instance_count());
        stat_row("Planets drawn", rs.planetsDrawn);
        if (rs.droppedPlanets > 0) {
            stat_row_threshold("Planets dropped", rs.droppedPlanets, 1, 1);
            tooltip("Instance budget (MAX_INSTANCES) reached, whole planet skipped");
        }

        ImGui::Separator();
        ratio_bar("Exact slice", rs.exactSlots, total, COLOR_OK);
        tooltip("Tile sampled its own heightmap. This is the target steady state");

        ratio_bar("Ancestor fallback", rs.fallbackSlots, total, COLOR_WARN);
        tooltip("Tile drew from a parent's slice, stretched with uvScale < 1.\n"
                "Transient while generation catches up, persistent means it never does");

        ratio_bar("No slice", rs.missingSlots, total, COLOR_BAD);
        tooltip("No slice at any level: the vertex shader forces height to 0,\n"
                "so the tile renders as a flat sphere patch");

        ImGui::Separator();
        stat_row("Deepest fallback", rs.deepestFallback);
        tooltip("Worst number of levels walked up this frame. Each level\n"
                "halves the effective heightmap resolution of that tile");

        stat_row("Uploads this frame", rs.uploadsThisFrame);
        tooltip("Results drained from the workers and written into the atlas.\n"
                "Capped at 8 per planet per frame by the drain() call");

        // ---- Frame to frame movement ---------------------------------------
        // A level tells nothing on its own: 300 fallbacks is fine if it is the same 300 tiles
        // waiting on generation, and pathological if it is a different 300 every frame. These
        // deltas are what separates the two
        const int64_t dExact = int64_t(rs.exactSlots) - int64_t(m_prevExact);
        const int64_t dFallback = int64_t(rs.fallbackSlots) - int64_t(m_prevFallback);
        const int64_t dMissing = int64_t(rs.missingSlots) - int64_t(m_prevMissing);
        const int64_t dLeaves = int64_t(m_lastLeafCount) - int64_t(m_prevLeafCount);
        const uint32_t evictionsThisFrame = as.evictions - m_prevEvictions;

        ImGui::Separator();
        ImGui::TextUnformatted("Change since last frame");
        delta_row("  d Exact", dExact);
        delta_row("  d Fallback", dFallback);
        delta_row("  d No slice", dMissing);
        delta_row("  d Leaves", dLeaves);
        stat_row("  Evictions", evictionsThisFrame);
        tooltip("Slices dropped this frame to make room. Evictions while the tree\n"
                "is quiet mean live tiles are being thrown out");

        // ---- Premature eviction test ---------------------------------------
        // The tree being quiet removes the legitimate reason for resolution to move: no split
        // means no new tile to resolve, no merge means no tile leaving the draw list. Anything
        // still swinging is the atlas dropping slices that the current frame is using
        const bool treeQuiet = dLeaves == 0;
        const uint32_t swing = uint32_t(std::abs(dFallback) + std::abs(dMissing));

        if (treeQuiet) {
            ++m_quietFrames;
            m_quietEvictions += evictionsThisFrame;
            m_worstQuietSwing = std::max(m_worstQuietSwing, swing);
        }

        ImGui::Separator();
        ImGui::TextUnformatted("Premature eviction test");
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        tooltip("Samples only the frames where the tree did not split or merge.\n"
                "With a stable tree the draw list is identical, so resolution\n"
                "should be identical too. A large worst swing means slices are\n"
                "evicted while still in use, and the tile falls back to an\n"
                "ancestor for reasons that have nothing to do with the LOD");

        stat_row("  Quiet frames", m_quietFrames);
        stat_row_threshold("  Worst quiet swing", m_worstQuietSwing, 1, 16);
        tooltip("Largest |d Fallback| + |d No slice| over a frame where the leaf\n"
                "count did not move. Should be 0 once generation has caught up.\n"
                "Anything sustained is the LRU protection failing");

        stat_row_threshold("  Quiet evictions", m_quietEvictions, 1, 64);
        tooltip("Evictions accumulated on frames with no split and no merge.\n"
                "The working set is not growing on those frames, so a rising\n"
                "count means the atlas is recycling tiles it still needs");

        if (ImGui::SmallButton("Reset test")) {
            m_worstQuietSwing = 0;
            m_quietEvictions = 0;
            m_quietFrames = 0;
        }

        // ---- History -------------------------------------------------------
        if (!m_historyPaused) {
            m_history.push(float(rs.exactSlots), float(rs.fallbackSlots), float(rs.missingSlots),
                           float(rs.uploadsThisFrame), float(std::abs(dLeaves)));
        }

        ImGui::Separator();
        ImGui::Checkbox("Pause history", &m_historyPaused);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear history"))
            m_history.clear();

        if (m_history.filled > 0) {
            const int count = m_history.filled;
            const int offset = m_history.filled == SlotHistory::CAPACITY ? m_history.cursor : 0;

            // One shared scale across the three resolution plots, so their relative size is
            // readable at a glance instead of each curve filling its own box
            const float slotScale =
                std::max({array_max(m_history.exact.data(), count), array_max(m_history.fallback.data(), count),
                          array_max(m_history.missing.data(), count), 1.f});

            char overlay[64];

            snprintf(overlay, sizeof(overlay), "exact  now %u", rs.exactSlots);
            history_plot("##histExact", m_history.exact.data(), count, offset, slotScale, COLOR_OK, overlay, 40.f);

            snprintf(overlay, sizeof(overlay), "fallback  now %u", rs.fallbackSlots);
            history_plot("##histFallback", m_history.fallback.data(), count, offset, slotScale, COLOR_WARN, overlay,
                         40.f);

            snprintf(overlay, sizeof(overlay), "no slice  now %u", rs.missingSlots);
            history_plot("##histMissing", m_history.missing.data(), count, offset, slotScale, COLOR_BAD, overlay, 40.f);

            // Drawn against the three above: spikes that line up with a topology change are the
            // tree refining, spikes that do not are the atlas losing slices on its own
            snprintf(overlay, sizeof(overlay), "leaf delta  now %+lld", static_cast<long long>(dLeaves));
            history_plot("##histTopology", m_history.topologyDelta.data(), count, offset,
                         std::max(array_max(m_history.topologyDelta.data(), count), 1.f), ImVec4(0.6f, 0.6f, 0.9f, 1.f),
                         overlay, 34.f);

            snprintf(overlay, sizeof(overlay), "uploads  now %u", rs.uploadsThisFrame);
            history_plot("##histUploads", m_history.uploads.data(), count, offset,
                         std::max(array_max(m_history.uploads.data(), count), 1.f), ImVec4(0.5f, 0.8f, 1.0f, 1.f),
                         overlay, 34.f);
        }

        m_prevExact = rs.exactSlots;
        m_prevFallback = rs.fallbackSlots;
        m_prevMissing = rs.missingSlots;
        m_prevLeafCount = m_lastLeafCount;
        m_prevEvictions = as.evictions;
    }
}
