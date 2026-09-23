#include "PlanetChunksDebug.h"

namespace vp {

void PlanetChunksDebug::render(flecs::world& ecs) {
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
        draw_store();
        draw_generator();
    }
}

void PlanetChunksDebug::draw_store() {
    ImGui::SeparatorText("Store");

    if (!m_selectedPlanet.store.has()) {
        ImGui::Text("Invalid store reference!");
        return;
    }

    const auto* store = m_selectedPlanet.store.get();
    ImGui::Text("Loaded Chunks: %lu", store->size());
}

void PlanetChunksDebug::draw_generator() {
    ImGui::SeparatorText("Generator");

    const auto* comp = m_selectedPlanet.generator.try_get();
    if (comp == nullptr || !comp->generator) {
        ImGui::Text("No generator");
        return;
    }

    const auto& gen = *comp->generator;
    const auto& s = gen.stats();

    ImGui::Text("Pending:   %zu (peak %u)", gen.pending_count(), s.peakPending);
    ImGui::Text("In flight: %zu (peak %u)", gen.in_flight_count(), s.peakInFlight);
}

void PlanetChunksDebug::select_planet(PlanetRepresentation planet) {
    m_selectedPlanet = planet;
}

void PlanetChunksDebug::collect_planets(flecs::world& ecs, std::vector<PlanetRepresentation>& planets) {
    // instance queries
    if (!m_planetQuery) {
        m_planetQuery = ecs.query<const Planet, const PlanetSurfaceChunkStore, const PlanetSurfaceChunkGeneratorComp>();
    }

    m_planetQuery.each([&](flecs::entity planet, const Planet&, const PlanetSurfaceChunkStore&,
                           const PlanetSurfaceChunkGeneratorComp&) {
        planets.push_back(PlanetRepresentation{.entity = planet,
                                               .store = planet.get_ref<const PlanetSurfaceChunkStore>(),
                                               .generator = planet.get_ref<const PlanetSurfaceChunkGeneratorComp>()});
    });
}

} // namespace vp
