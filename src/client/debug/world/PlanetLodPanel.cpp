#include "PlanetLodPanel.h"

#include <cmath>

#include "imgui.h"
#include "client/world/planet/planet_client_components.h"
#include "core/TracyIntegration.h"
#include "core/world/planet/planet_transform.h"

using namespace vp;

namespace {
    void stat_row(const char *label, uint32_t value) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine(190.f);
        ImGui::Text("%u", value);
    }
}

void PlanetLodPanel::render(flecs::world &ecs) {
    VOXEL_ZONE_N("PlanetLodPanel-Display");

    bool found = false;
    ecs.each([&](flecs::entity e, PlanetTileLodComp &lod) {
        found = true;
        if (!lod.quadtree) return;

        auto &params = lod.params;
        const auto &stats = lod.quadtree->stats();

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
            ImGui::DragScalar("Split factor", ImGuiDataType_Double, &params.splitFactor,
                              0.02f, nullptr, nullptr, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("A node splits while distance < factor * node size.\n"
                                  "Higher means more detail and more nodes");
            }

            ImGui::DragScalar("Merge hysteresis", ImGuiDataType_Double, &params.mergeHysteresis,
                              0.01f, nullptr, nullptr, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Merge beyond splitFactor * hysteresis.\n"
                                  "1.0 makes a hovering camera split and merge every frame");
            }

            ImGui::DragScalar("Face Cull Angle Deg", ImGuiDataType_Double, &params.faceCullAngleDeg,
                              0.5f, nullptr, nullptr, "%.1f deg");

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
            ImGui::TextColored(aligned ? ImVec4(0.2f, 0.9f, 0.3f, 1.f) : ImVec4(1.0f, 0.6f, 0.1f, 1.f),
                               "%.4f m", voxel);
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
            stat_row("Balance splits", stats.balanceSplits);
        }
    });

    if (!found) {
        ImGui::TextDisabled("No planet with a PlanetTileLodComp in the world");
    }
}
