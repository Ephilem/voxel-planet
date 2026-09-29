#include "PlanetChunksRenderingDebug.h"

#include "renderer/world/planet/planet_rendering_components.h"
#include "renderer/world/planet/PlanetSurfaceChunkMesher.h"
#include "renderer/world/planet/PlanetSurfaceChunkRenderer.h"

namespace vp {

void PlanetChunksRenderingDebug::render(flecs::world& ecs) {
    const auto* ref = ecs.get<PlanetSurfaceChunkRenderingRef>();
    if (ref == nullptr || ref->mesher == nullptr || ref->renderer == nullptr) {
        ImGui::Text("No surface chunk renderer");
        return;
    }

    if (!m_storeQuery) {
        m_storeQuery = ecs.query<const PlanetSurfaceChunkStore>();
    }

    size_t storeTotal = 0;
    size_t storeAllocated = 0;
    m_storeQuery.each([&](const PlanetSurfaceChunkStore& store) {
        storeTotal += store.size();
        for (const auto& [key, chunk] : store.chunks()) {
            if (chunk && chunk->is_allocated()) {
                ++storeAllocated;
            }
        }
    });

    const auto& mesher = *ref->mesher;
    const auto& m = mesher.stats();
    const auto& b = ref->renderer->chunk_buffer().stats();

    ImGui::SeparatorText("Store");
    ImGui::Text("Chunks:          %zu", storeTotal);
    ImGui::Text("Allocated:       %zu", storeAllocated);

    ImGui::SeparatorText("Mesher");
    ImGui::Text("In flight:       %zu", mesher.in_flight_count());
    ImGui::Text("Results waiting: %zu", mesher.queued_results());
    ImGui::Text("Skipped (no data): %u", m.skippedUnallocated);
    ImGui::Text("Empty meshes:    %u", m.emptyMeshes);
    ImGui::Text("Discarded stale: %u", m.discardedStale);

    ImGui::SeparatorText("GPU buffer");
    ImGui::Text("Resident chunks: %u", b.residentChunks);
    ImGui::Text("Used vertices:   %u / %u reserved", b.usedVertices, ref->renderer->chunk_buffer().get_max_vertices());
    ImGui::Text("Failed allocs:   %u", b.failedAllocations);
}

} // namespace vp
