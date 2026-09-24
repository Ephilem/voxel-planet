#include "PlanetTilesDebug.h"

#include <cfloat>
#include <format>

#include "renderer/world/planet/PlanetTileAtlas.h"
#include "renderer/world/planet/PlanetTileRenderer.h"

namespace vp {

void PlanetTilesDebug::render(flecs::world& ecs) {
    std::vector<PlanetRepresentation> planets;
    collect_planets(ecs, planets);

    if (!m_selectedPlanet.entity.is_alive() && !planets.empty()) {
        select_planet(planets.front());
    }

    ImGui::Text("Select a planet:");
    ImGui::SameLine();
    const char* preview = m_selectedPlanet.entity.is_alive() ? m_selectedPlanet.entity.name() : "Select a planet";

    if (ImGui::BeginCombo("##planetSelect", preview)) {
        for (const auto& planet : planets) {
            const bool isSelected = (m_selectedPlanet.entity == planet.entity);

            if (ImGui::Selectable(planet.entity.name().c_str(), isSelected)) {
                select_planet(planet);
            }

            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    if (m_selectedPlanet.entity.is_alive()) {
        draw_lod();
        draw_generator();
    }

    draw_atlas(ecs);
}

void PlanetTilesDebug::draw_lod() {
    ImGui::SeparatorText("LOD");

    const auto* lod = m_selectedPlanet.lod.try_get();
    if (lod == nullptr || !lod->quadtree) {
        ImGui::Text("No quadtree");
        return;
    }

    const auto& s = lod->quadtree->stats();
    const auto* drawList = m_selectedPlanet.drawList.try_get();

    ImGui::Text("Leaves:        %u", s.leafCount);
    ImGui::Text("Draw items:    %zu", drawList ? drawList->drawItems.size() : 0);
    ImGui::Text("Splits/merges: %u / %u (balance %u)", s.splits, s.merges, s.balanceSplits);
}

void PlanetTilesDebug::draw_generator() {
    ImGui::SeparatorText("Generator");

    const auto* stream = m_selectedPlanet.stream.try_get();
    if (stream == nullptr || !stream->generator) {
        ImGui::Text("No generator");
        return;
    }

    const auto& gen = *stream->generator;
    const auto& s = gen.stats();

    ImGui::Text("Pending:   %zu (peak %u)", gen.pending(), s.peakPending);
    ImGui::Text("In flight: %zu (peak %u)", gen.in_flight(), s.peakInFlight);
}

void PlanetTilesDebug::draw_atlas(flecs::world& ecs) {
    ImGui::SeparatorText("Atlas");

    const auto* ref = ecs.get<PlanetTileAtlasRef>();
    if (ref == nullptr || ref->atlas == nullptr) {
        ImGui::Text("No atlas");
        return;
    }

    const auto& a = ref->atlas->stats();

    const float fill = a.capacity > 0 ? float(a.resident) / float(a.capacity) : 0.f;
    ImGui::Text("Resident:");
    ImGui::SameLine();
    ImGui::ProgressBar(fill, ImVec2(-1, 0), std::format("{} / {}", a.resident, a.capacity).c_str());

    ImGui::Text("Uploads:   %u", a.uploads);
    ImGui::Text("Evictions: %u", a.evictions);
    ImGui::Text("Expired:   %u", a.expired);
    ImGui::Text("Failed:    %u", a.failedUploads);

    if (ref->renderer == nullptr) {
        return;
    }

    ImGui::SeparatorText("Slot resolution");

    const auto& r = ref->renderer->stats();
    const uint32_t total = ref->renderer->last_instance_count();

    auto percent = [total](uint32_t v) { return total > 0 ? 100.f * float(v) / float(total) : 0.f; };

    ImGui::Text("Instances: %u", total);
    ImGui::Text("Exact:     %u (%.0f%%)", r.exactSlots, percent(r.exactSlots));
    ImGui::Text("Fallback:  %u (%.0f%%), deepest %u", r.fallbackSlots, percent(r.fallbackSlots), r.deepestFallback);
    ImGui::Text("Missing:   %u (%.0f%%)", r.missingSlots, percent(r.missingSlots));
    ImGui::Text("Uploads this frame: %u / %u", r.uploadsThisFrame, PlanetTileRenderer::MAX_TILE_UPLOADS_PER_FRAME);
}

void PlanetTilesDebug::select_planet(PlanetRepresentation planet) {
    m_selectedPlanet = planet;
}

void PlanetTilesDebug::collect_planets(flecs::world& ecs, std::vector<PlanetRepresentation>& planets) {
    // instance queries
    if (!m_planetQuery) {
        m_planetQuery = ecs.query<const Planet, const PlanetTileLodComp, const PlanetTileStreamComp>();
    }

    m_planetQuery.each([&](flecs::entity planet, const Planet&, const PlanetTileLodComp&, const PlanetTileStreamComp&) {
        planets.push_back(PlanetRepresentation{.entity = planet,
                                               .lod = planet.get_ref<const PlanetTileLodComp>(),
                                               .stream = planet.get_ref<const PlanetTileStreamComp>(),
                                               .drawList = planet.get_ref<const PlanetTileDrawListComp>()});
    });
}

} // namespace vp
