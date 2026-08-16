#include "PlanetChunkGenPanel.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

#include "core/TracyIntegration.h"
#include "core/world/planet/planet_components.h"
#include "core/world/planet/planet_types.h"
#include "core/world/planet/PlanetSurfaceChunkGenerator.h"
#include "core/world/planet/PlanetSurfaceChunkStore.h"
#include "imgui.h"

using namespace vp;

namespace {
constexpr float VALUE_COLUMN = 190.f;

const ImVec4 COLOR_OK{0.2f, 0.9f, 0.3f, 1.f};
const ImVec4 COLOR_WARN{1.0f, 0.6f, 0.1f, 1.f};
const ImVec4 COLOR_BAD{1.0f, 0.3f, 0.25f, 1.f};
const ImVec4 COLOR_INFO{0.5f, 0.8f, 1.0f, 1.f};

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

/// Signed delta, coloured by direction and hidden when nothing moved
void delta_row(const char* label, int64_t delta) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(VALUE_COLUMN);
    if (delta == 0) {
        ImGui::TextDisabled("0");
    } else {
        ImGui::TextColored(delta > 0 ? COLOR_OK : COLOR_WARN, "%+lld", static_cast<long long>(delta));
    }
}

/**
 * Plots a ring buffer in chronological order.
 *
 * ImGui walks the array linearly, so the write cursor is handed over as an offset to keep the
 * oldest sample on the left instead of a discontinuity wandering through the plot
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

void key_row(const char* label, const PlanetSurfaceChunkKey& key) {
    ImGui::TextUnformatted(label);
    ImGui::SameLine(VALUE_COLUMN);
    if (!key.valid()) {
        ImGui::TextDisabled("invalid (face ??)");
        return;
    }
    ImGui::Text("%s  x %d  y %d  alt %d  lvl %u", face_name(key.face), key.x, key.y, key.alt, uint32_t(key.level));
}
} // namespace

void PlanetChunkGenPanel::render(flecs::world& ecs) {
    VOXEL_ZONE_N("PlanetChunkGenPanel-Display");

    bool found = false;
    bool primary = true;

    ecs.each([&](flecs::entity e, PlanetSurfaceChunkGeneratorComp& gen) {
        found = true;
        if (!gen.generator)
            return;

        ImGui::PushID(static_cast<int>(e.id()));
        render_generator_section(e, primary);
        ImGui::PopID();

        // Only the first planet drives the panel's own history and stall tracking: those are single
        // buffers, and interleaving two planets into them would make both unreadable
        primary = false;
    });

    if (!found) {
        ImGui::TextDisabled("No planet with a PlanetSurfaceChunkGeneratorComp in the world");
        return;
    }

    render_loader_section(ecs);
}

void PlanetChunkGenPanel::render_generator_section(flecs::entity planet, bool primary) {
    const auto& gen = *planet.get<PlanetSurfaceChunkGeneratorComp>()->generator;
    const auto& gs = gen.stats();

    // The generator publishes cumulative counters only, but the stages are strictly ordered:
    // everything requested is either still pending or already submitted, and everything submitted
    // is either in flight or already completed. That makes both depths exact, not estimates
    const uint32_t pending = gs.requested - gs.submitted;
    const uint32_t inFlight = gs.submitted - gs.completed;

    const auto* store = planet.get<PlanetSurfaceChunkStore>();
    const size_t storeSize = store ? store->size() : 0;

    // name() hands back a non owning view into the world's storage, so keeping the char pointer
    // past the temporary is safe
    const char* planetName = planet.name();
    ImGui::SeparatorText(planetName && planetName[0] ? planetName : "planet");

    // ---- Health ------------------------------------------------------------
    // Drawn first and unconditionally: a broken pipeline makes every counter below meaningless,
    // and the failure modes here all look like "quiet" from the counters alone
    if (primary) {
        const uint32_t completedThisFrame = gs.completed - m_prevCompleted;

        if (inFlight > 0 && completedThisFrame == 0) {
            ++m_stallFrames;
            m_worstStallFrames = std::max(m_worstStallFrames, m_stallFrames);
        } else {
            m_stallFrames = 0;
        }
    }

    if (gs.submitted > 0 && gs.completed == 0) {
        ImGui::TextColored(COLOR_BAD, "No result has ever been drained");
        tooltip("Work reaches the workers but drain() is never called, so results pile up in\n"
                "the result queue, m_inFlight never shrinks, and the store stays empty.\n"
                "Every chunk is generated and then thrown away");
    } else if (primary && m_stallFrames > 120) {
        ImGui::TextColored(COLOR_WARN, "Stalled: %u frames with work in flight and no completion", m_stallFrames);
        tooltip("Generating one chunk is a few milliseconds, so a stall this long is not the\n"
                "workers being slow: either drain() is not called this frame, or a worker is\n"
                "blocked inside generate()");
    } else if (gs.completed > 0 && storeSize == 0) {
        ImGui::TextColored(COLOR_WARN, "Chunks complete but the store is empty");
        tooltip("drain() returns results that are never handed to PlanetSurfaceChunkStore::store()");
    }

    // ---- This frame --------------------------------------------------------
    if (ImGui::CollapsingHeader("Throughput (this frame)", ImGuiTreeNodeFlags_DefaultOpen)) {
        stat_row("Requested", gs.requestedThisFrame);
        tooltip("Accepted into m_pending. Spikes only when the loader crosses a chunk\n"
                "boundary: RequestChunks early-outs while the centre key is unchanged");

        stat_row("Submitted", gs.submittedThisFrame);
        tooltip("Handed to the workers, capped by the maxSubmit argument of\n"
                "submit_pending() (32 by default), cheapest priority first");

        stat_row("Completed", gs.completedThisFrame);
        tooltip("Drained back on the main thread, capped by the maxDrain argument\n"
                "of drain() (16 by default)");

        // A submit budget permanently smaller than the request rate is a backlog that can only
        // grow, and no amount of worker throughput fixes it
        if (gs.requestedThisFrame > gs.submittedThisFrame && pending > 0) {
            ImGui::TextDisabled("%u request(s) deferred to a later frame",
                                gs.requestedThisFrame - gs.submittedThisFrame);
        }
    }

    // ---- Queue depths ------------------------------------------------------
    if (ImGui::CollapsingHeader("Queue depths", ImGuiTreeNodeFlags_DefaultOpen)) {
        stat_row_threshold("Pending (queued)", pending, 256, 2048);
        tooltip("requested - submitted. Chunks accepted but not yet released to the\n"
                "workers. Drains at maxSubmit per frame, so a deep queue is latency:\n"
                "1000 pending at 32/frame is half a second before the last one starts");

        stat_row_threshold("In flight", inFlight, 128, 512);
        tooltip("submitted - completed. Held by a worker or sitting in the result\n"
                "queue waiting to be drained. Bounded by the workers only if drain()\n"
                "keeps up: otherwise this is where everything accumulates");

        ImGui::Separator();
        stat_row("Peak pending", gs.peakPending);
        stat_row("Peak in flight", gs.peakInFlight);
        tooltip("Peaks are cumulative over the session and never reset, so they record\n"
                "the worst burst even if it happened while the panel was closed");
    }

    // ---- Totals ------------------------------------------------------------
    if (ImGui::CollapsingHeader("Totals", ImGuiTreeNodeFlags_DefaultOpen)) {
        stat_row("Requested", gs.requested);
        stat_row("Submitted", gs.submitted);
        stat_row("Completed", gs.completed);

        ImGui::Separator();
        stat_row("Dedup rejects", gs.dedupRejects);
        tooltip("request() calls dropped because the key was already pending or in\n"
                "flight. The loader re-walks its whole sphere on every centre change,\n"
                "so a large count is expected, not a symptom");

        // The share of calls that survive dedup says how much of the sphere walk is actually new
        // work. Near zero means the loader is re-requesting a set it already has in the pipeline
        const uint32_t calls = gs.requested + gs.dedupRejects;
        if (calls > 0) {
            const float accepted = float(gs.requested) / float(calls);
            ImGui::TextUnformatted("Accept ratio");
            ImGui::SameLine(VALUE_COLUMN);
            ImGui::TextColored(accepted < 0.05f ? COLOR_WARN : COLOR_OK, "%.1f%%", accepted * 100.f);
            tooltip("requested / (requested + dedupRejects)");
        }
    }

    // ---- Store -------------------------------------------------------------
    if (ImGui::CollapsingHeader("Chunk store", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!store) {
            ImGui::TextDisabled("No PlanetSurfaceChunkStore on this planet");
        } else {
            stat_row("Resident chunks", uint32_t(storeSize));

            if (primary) {
                delta_row("  since last frame", int64_t(storeSize) - int64_t(m_prevStoreSize));
                m_prevStoreSize = storeSize;
            }

            // Upper bound: a chunk the generator rejected on the altitude test carries no voxel
            // array at all, and two chunks can share one through the copy-on-write shared_ptr
            constexpr size_t CHUNK_BYTES = size_t(CHUNK_VOLUME) * sizeof(uint16_t);
            stat_row_fmt("RAM footprint", "<= %.1f MiB", double(storeSize * CHUNK_BYTES) / (1024.0 * 1024.0));
            tooltip("Resident chunks x 64 KiB. An upper bound: empty chunks keep their\n"
                    "voxel array unallocated, and set() shares it until a write");

            // Chunks that completed but are not in the store were either empty or dropped
            if (gs.completed >= uint32_t(storeSize)) {
                stat_row("Completed, not stored", gs.completed - uint32_t(storeSize));
                tooltip("Chunks the generator finished that are not resident: empty chunks\n"
                        "above the terrain, plus anything evicted by the unload pass");
            }
        }
    }

    if (!primary)
        return;

    // ---- History -----------------------------------------------------------
    if (ImGui::CollapsingHeader("History", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (!m_historyPaused) {
            m_history.push(float(gs.requestedThisFrame), float(gs.submittedThisFrame), float(gs.completedThisFrame),
                           float(pending), float(inFlight));
        }

        ImGui::Checkbox("Pause history", &m_historyPaused);
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear history"))
            m_history.clear();

        if (m_history.filled > 0) {
            const int count = m_history.filled;
            const int offset = m_history.filled == History::CAPACITY ? m_history.cursor : 0;

            // One shared scale for the three rates, so submit starving the request rate is
            // readable at a glance instead of each curve filling its own box
            const float rateScale =
                std::max({array_max(m_history.requested.data(), count), array_max(m_history.submitted.data(), count),
                          array_max(m_history.completed.data(), count), 1.f});

            char overlay[64];

            snprintf(overlay, sizeof(overlay), "requested/f  now %u", gs.requestedThisFrame);
            history_plot("##histRequested", m_history.requested.data(), count, offset, rateScale, COLOR_INFO, overlay,
                         40.f);

            snprintf(overlay, sizeof(overlay), "submitted/f  now %u", gs.submittedThisFrame);
            history_plot("##histSubmitted", m_history.submitted.data(), count, offset, rateScale, COLOR_WARN, overlay,
                         40.f);

            snprintf(overlay, sizeof(overlay), "completed/f  now %u", gs.completedThisFrame);
            history_plot("##histCompleted", m_history.completed.data(), count, offset, rateScale, COLOR_OK, overlay,
                         40.f);

            // The depths share their own scale: they are a backlog, orders of magnitude away from
            // the per frame rates above, and squashing both into one axis flattens the rates
            const float depthScale = std::max({array_max(m_history.pending.data(), count),
                                               array_max(m_history.inFlight.data(), count), 1.f});

            snprintf(overlay, sizeof(overlay), "pending  now %u", pending);
            history_plot("##histPending", m_history.pending.data(), count, offset, depthScale,
                         ImVec4(0.6f, 0.6f, 0.9f, 1.f), overlay, 34.f);

            snprintf(overlay, sizeof(overlay), "in flight  now %u", inFlight);
            history_plot("##histInFlight", m_history.inFlight.data(), count, offset, depthScale,
                         ImVec4(0.9f, 0.5f, 0.9f, 1.f), overlay, 34.f);
        }
    }

    // ---- Stall test --------------------------------------------------------
    if (ImGui::CollapsingHeader("Stall test", ImGuiTreeNodeFlags_DefaultOpen)) {
        stat_row_threshold("Frames without completion", m_stallFrames, 60, 300);
        tooltip("Consecutive frames with work in flight and no result drained.\n"
                "Non-zero is normal while a worker is mid-chunk, sustained is not");

        stat_row_threshold("Worst streak", m_worstStallFrames, 60, 300);

        if (ImGui::SmallButton("Reset stall test")) {
            m_stallFrames = 0;
            m_worstStallFrames = 0;
        }
    }

    m_prevCompleted = gs.completed;
}

void PlanetChunkGenPanel::render_loader_section(flecs::world& ecs) {
    bool found = false;

    ecs.each([&](flecs::entity e, PlanetChunkLoader& loader) {
        found = true;
        ImGui::PushID(static_cast<int>(e.id()));

        const char* loaderName = e.name();
        ImGui::SeparatorText(loaderName && loaderName[0] ? loaderName : "loader");

        if (ImGui::CollapsingHeader("Loader", ImGuiTreeNodeFlags_DefaultOpen)) {
            // The request count grows with the cube of the radius, so the slider is worth reading
            // together with the volume below before dragging it
            int loading = int(loader.loadingDistance);
            if (ImGui::SliderInt("Loading radius", &loading, 0, 32))
                loader.loadingDistance = uint8_t(loading);
            tooltip("Chunks requested around the observer, as a sphere radius");

            int unloading = int(loader.unloadingDistance);
            if (ImGui::SliderInt("Unloading radius", &unloading, 0, 48))
                loader.unloadingDistance = uint8_t(unloading);
            tooltip("Chunks beyond this are dropped from the store. The unload pass is\n"
                    "commented out in PlanetModule::RequestChunks, so this is inert for now");

            int altitude = int(loader.altitudeDistance);
            if (ImGui::SliderInt("Altitude radius", &altitude, 0, 16))
                loader.altitudeDistance = uint8_t(altitude);
            tooltip("Vertical extent of the request box, in chunks, before the spherical\n"
                    "distance test trims it");

            ImGui::Separator();

            // What one boundary crossing costs: the loop is a box trimmed to a sphere, so the
            // count is the sphere volume clipped by the altitude slab. Cached because the walk is
            // the same cubic loop the loader runs, and at radius 32 that is 275k iterations of
            // debug UI per frame
            const int R = int(loader.loadingDistance);
            const int altR = int(loader.altitudeDistance);
            if (R != m_cachedRadius || altR != m_cachedAltitude) {
                m_cachedRadius = R;
                m_cachedAltitude = altR;
                m_cachedCandidates = 0;
                for (int dz = -altR; dz <= altR; ++dz)
                    for (int dy = -R; dy <= R; ++dy)
                        for (int dx = -R; dx <= R; ++dx)
                            if ((dx * dx) + (dy * dy) + (dz * dz) <= R * R)
                                ++m_cachedCandidates;
            }
            const uint32_t candidates = m_cachedCandidates;

            stat_row("Chunks per refresh", candidates);
            tooltip("Keys walked on every centre change, before the store and dedup\n"
                    "filters. This is the burst that lands in one frame");

            stat_row_fmt("Voxels per refresh", "%.1f M", double(candidates) * double(CHUNK_VOLUME) / 1e6);

            ImGui::Separator();
            key_row("Last centre", loader.lastCenter);
            tooltip("RequestChunks early-outs while the observer stays in this chunk,\n"
                    "so nothing is requested until it changes");

            if (ImGui::SmallButton("Force refresh")) {
                // Invalidating the centre makes the next tick take the slow path and re-walk the
                // sphere, which is the only way to re-request after a tuning change
                loader.lastCenter = PlanetSurfaceChunkKey{};
            }
            tooltip("Invalidate the cached centre so the next frame re-walks the sphere.\n"
                    "Use it after moving a slider: the radii only take effect on a refresh");
        }

        ImGui::PopID();
    });

    if (!found)
        ImGui::TextDisabled("No entity with a PlanetChunkLoader (nothing is requesting chunks)");
}
