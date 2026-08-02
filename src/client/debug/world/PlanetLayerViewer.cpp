#include "PlanetLayerViewer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <limits>
#include <random>
#include <string>

#include "PlanetDebugLayer.h"
#include "PlanetLayerProjectionMappings.h"
#include "core/log/Logger.h"
#include "renderer/Renderer.h"

using namespace vp;

constexpr float NAN_VALUE = std::numeric_limits<float>::quiet_NaN();

const int RESOLUTION_STEPS[] = {64, 128, 256, 512, 1024};
constexpr int RESOLUTION_STEP_COUNT = 5;

void PlanetLayerViewer::draw_params() {
    if (!m_selectedPlanet.entity.is_alive()) {
        ImGui::Text("No planet selected!");
        return;
    }

    PlanetTerrainParams &p = m_selectedPlanet.terrainParams;
    bool changed = false;

    ImGui::SeparatorText("Global");
    changed |= ImGui::SliderFloat("Sea level", &p.seaLevel, -2000.f, 2000.f, "%.1f m");

    ImGui::SeparatorText("Continents");
    changed |= ImGui::SliderFloat("Continent amplitude", &p.continentAmplitude, 0.f, 4000.f, "%.1f m");
    changed |= ImGui::SliderFloat("Continent frequency", &p.continentFrequency, 0.01f, 10.f, "%.3f", ImGuiSliderFlags_Logarithmic);
    changed |= ImGui::SliderInt("Continent octaves", &p.continentOctave, 1, p.maxOctave);

    ImGui::SeparatorText("Mountains");
    changed |= ImGui::SliderFloat("Mountain amplitude", &p.mountainAmplitude, 0.f, 3000.f, "%.1f m");
    changed |= ImGui::SliderFloat("Mountain frequency", &p.mountainFrequency, 0.01f, 30.f, "%.3f", ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText("Noise");
    changed |= ImGui::SliderInt("Max octaves", &p.maxOctave, 1, 16);
    changed |= ImGui::InputInt("Seed", &p.seed);
    ImGui::SameLine();
    if (ImGui::Button("Random")) {
        p.seed = static_cast<int32_t>(std::random_device{}());
        changed |= true;
    }

    p.continentOctave = std::clamp(p.continentOctave, 1, p.maxOctave);

    m_dirty |= changed;

    ImGui::SeparatorText("View");
    int resIndex = 0;
    for (int i = 0; i < RESOLUTION_STEP_COUNT; ++i) {
        if (RESOLUTION_STEPS[i] == m_resolution)
            resIndex = i;
    }

    char resLabel[32];
    std::snprintf(resLabel, sizeof(resLabel), "%d px/face", RESOLUTION_STEPS[resIndex]);

    if (ImGui::SliderInt("Resolution", &resIndex, 0, RESOLUTION_STEP_COUNT - 1, resLabel)) {
        resIndex = std::clamp(resIndex, 0, RESOLUTION_STEP_COUNT - 1);
        m_resolution = RESOLUTION_STEPS[resIndex];
        m_needsRegen = true;
    }

    if (ImGui::SliderInt("LOD", &m_lod, 0, 12))
        m_needsRegen = true;
    ImGui::SetItemTooltip("Octave count = clamp(3 + LOD, 1, maxOctave).\n"
                          "Raise it to judge the fine relief.");

    ImGui::Spacing();
    if (ImGui::Button("Apply"))
        apply_generation_params();

    ImGui::SameLine();
    if (m_dirty)
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "Modified parameters!");

    if (m_imageValid) {
        ImGui::Spacing();
        ImGui::SeparatorText("Last generation");
        ImGui::Text("%d x %d px", m_imageSize.x, m_imageSize.y);
        ImGui::Text("Generated in %.1f ms", m_lastGenerationMs);
        ImGui::Text("Range [%.1f, %.1f]", m_valueMin, m_valueMax);
    }
}

void PlanetLayerViewer::draw_controls(flecs::world &ecs) {
    std::vector<PlanetRepresentation> planets;
    collect_planets(ecs, planets);

    if (!m_selectedPlanet.entity.is_alive() && !planets.empty())
        select_planet(planets.front());

    ImGui::Text("Select a planet to view its layers:");
    ImGui::SameLine();
    const char* preview = m_selectedPlanet.entity.is_alive()
            ? m_selectedPlanet.entity.name()
            : "Select a planet";

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##planetSelect", preview)) {
        for (const auto &planet : planets) {
            const bool isSelected = (m_selectedPlanet.entity == planet.entity);

            if (ImGui::Selectable(planet.entity.name().c_str(), isSelected))
                select_planet(planet);

            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SameLine();

    const PlanetLayerProjectionMapping *selectedProj = projection_at(m_projIndex);

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##projSelect", selectedProj->name())) {
        for (int i = 0; i < projection_count(); ++i) {
            const PlanetLayerProjectionMapping *proj = projection_at(i);
            const bool isSelected = (m_projIndex == i);

            if (ImGui::Selectable(proj->name(), isSelected)) {
                m_projIndex = i;
                m_needsRegen = true;
            }

            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();

    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::BeginCombo("##layerSelect", layer_name_at(m_layerIndex))) {
        for (int i = 0; i < layer_count(); ++i) {
            const bool isSelected = (m_layerIndex == i);

            if (ImGui::Selectable(layer_name_at(i), isSelected)) {
                m_layerIndex = i;
                m_needsRegen = true;
            }

            if (isSelected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void PlanetLayerViewer::generate_texture() {
    const PlanetLayerProjectionMapping *proj = projection_at(m_projIndex);
    const PlanetLayerDebug layer = layer_at(m_layerIndex);
    if (!proj || !layer.evaluate)
        return;

    const auto start = std::chrono::steady_clock::now();

    m_imageSize = proj->image_size(m_resolution);
    if (m_imageSize.x <= 0 || m_imageSize.y <= 0)
        return;

    m_values.assign(static_cast<size_t>(m_imageSize.x) * m_imageSize.y, NAN_VALUE);

    PlanetTerrainSampler sampler(m_appliedParams);

    proj->tiles(m_resolution, m_tiles);

    for (const PlanetLayerProjectionTile &tile : m_tiles) {
        proj->fill_tile_directions(tile, m_field);

        const int count = m_field.count();
        if (count <= 0)
            continue;

        m_tileValues.resize(static_cast<size_t>(count) + PlanetTerrainSampler::SIMD_PADDING);
        layer.evaluate(sampler, m_field, m_lod, m_tileValues.data());

        for (int py = 0; py < tile.size.y; ++py) {
            const int dstY = tile.origin.y + py;
            if (dstY < 0 || dstY >= m_imageSize.y)
                continue;

            const float *src = m_tileValues.data() + static_cast<size_t>(py) * tile.size.x;
            float *dst = m_values.data() + static_cast<size_t>(dstY) * m_imageSize.x + tile.origin.x;

            const int copyWidth = std::min(tile.size.x, m_imageSize.x - tile.origin.x);
            if (copyWidth > 0)
                std::copy_n(src, copyWidth, dst);
        }
    }

    colorize(layer);

    const auto end = std::chrono::steady_clock::now();
    m_lastGenerationMs = std::chrono::duration<double, std::milli>(end - start).count();

    m_imageValid = true;
    m_textureDirty = true;
}

void PlanetLayerViewer::colorize(const PlanetLayerDebug &layer) {
    const size_t n = m_values.size();
    m_pixels.assign(n * 4, 0);

    float vmin = layer.rangeMin;
    float vmax = layer.rangeMax;

    if (layer.autoRange) {
        vmin = std::numeric_limits<float>::max();
        vmax = std::numeric_limits<float>::lowest();

        for (const float v : m_values) {
            if (std::isnan(v))
                continue;
            vmin = std::min(vmin, v);
            vmax = std::max(vmax, v);
        }

        if (vmin > vmax) {
            vmin = 0.0f;
            vmax = 0.0f;
        }
    }

    if (layer.divergingAroundZero) {
        const float m = std::max(std::abs(vmin), std::abs(vmax));
        vmin = -m;
        vmax = m;
    }

    m_valueMin = vmin;
    m_valueMax = vmax;

    const float inv = (vmax > vmin) ? 1.0f / (vmax - vmin) : 0.0f;

    ImPlot::PushColormap(layer.colormap);
    for (size_t i = 0; i < n; ++i) {
        const float v = m_values[i];
        uint8_t *px = &m_pixels[i * 4];

        if (std::isnan(v)) {
            px[0] = px[1] = px[2] = px[3] = 0; // transparent gap
            continue;
        }

        float t = std::clamp((v - vmin) * inv, 0.0f, 1.0f);
        if (!layer.interpolable)
            t = t > 0.5f ? 1.0f : 0.0f;
        if (layer.invertColormap)
            t = 1.0f - t;

        const ImVec4 c = ImPlot::SampleColormap(t, layer.colormap);
        px[0] = static_cast<uint8_t>(std::clamp(c.x, 0.0f, 1.0f) * 255.0f);
        px[1] = static_cast<uint8_t>(std::clamp(c.y, 0.0f, 1.0f) * 255.0f);
        px[2] = static_cast<uint8_t>(std::clamp(c.z, 0.0f, 1.0f) * 255.0f);
        px[3] = 255;
    }
    ImPlot::PopColormap();
}

float PlanetLayerViewer::value_at(int x, int y) const {
    if (!m_imageValid || x < 0 || y < 0 || x >= m_imageSize.x || y >= m_imageSize.y)
        return NAN_VALUE;

    return m_values[static_cast<size_t>(y) * m_imageSize.x + x];
}

bool PlanetLayerViewer::plot_to_pixel(const PlanetLayerProjectionMapping &proj,
                                      const ImPlotPoint &mouse, int &outX, int &outY) const {
    if (!m_imageValid)
        return false;

    double xMin, xMax, yMin, yMax;
    proj.plot_bounds(m_imageSize, xMin, xMax, yMin, yMax);

    if (xMax == xMin || yMax == yMin)
        return false;

    const double yTop = std::max(yMin, yMax);
    const double yBottom = std::min(yMin, yMax);

    const double u = (mouse.x - xMin) / (xMax - xMin);
    const double v = (yTop - mouse.y) / (yTop - yBottom);

    if (u < 0.0 || u >= 1.0 || v < 0.0 || v >= 1.0)
        return false;

    outX = std::clamp(static_cast<int>(u * m_imageSize.x), 0, m_imageSize.x - 1);
    outY = std::clamp(static_cast<int>(v * m_imageSize.y), 0, m_imageSize.y - 1);
    return true;
}

void PlanetLayerViewer::draw_cursor_readout(const PlanetLayerProjectionMapping &proj,
                                            const ImPlotPoint &mouse) const {
    int px = 0, py = 0;
    if (!plot_to_pixel(proj, mouse, px, py))
        return;

    const std::string where = proj.describe_pixel(px, py, m_imageSize);
    if (where == "-")
        return;

    const float value = value_at(px, py);
    const PlanetLayerDebug layer = layer_at(m_layerIndex);

    ImGui::BeginTooltip();
    ImGui::TextUnformatted(where.c_str());
    if (std::isnan(value))
        ImGui::TextDisabled("no data");
    else
        ImGui::Text("%s: %.2f %s", layer.name, value, layer.unit);
    ImGui::EndTooltip();
}

void PlanetLayerViewer::draw_map(flecs::world &ecs) {
    if (!m_selectedPlanet.entity.is_alive()) {
        ImGui::Text("No planet selected!");
        return;
    }

    if (m_needsRegen) {
        generate_texture();
        m_needsRegen = false;
    }

    if (m_textureDirty) {
        if (Renderer *renderer = ecs.get_mut<Renderer>()) {
            if (VulkanBackend *backend = renderer->backend.get()) {
                if (!m_mapTexture.upload(backend, m_pixels.data(),
                                         static_cast<uint32_t>(m_imageSize.x),
                                         static_cast<uint32_t>(m_imageSize.y)))
                    LOG_ERROR("PlanetLayerViewer", "Failed to upload the {}x{} layer texture",
                              m_imageSize.x, m_imageSize.y);
            }
        }
        m_textureDirty = false;
    }

    if (!m_mapTexture.valid()) {
        ImGui::Text("No texture available");
        return;
    }

    const PlanetLayerProjectionMapping *proj = projection_at(m_projIndex);
    const PlanetLayerDebug layer = layer_at(m_layerIndex);

    double xMin, xMax, yMin, yMax;
    proj->plot_bounds(m_imageSize, xMin, xMax, yMin, yMax);

    if (ImPlot::BeginPlot("##layerMap", ImVec2(-1, -1), ImPlotFlags_Equal | ImPlotFlags_NoLegend)) {
        ImPlot::SetupAxes(proj->x_axis_label(), proj->y_axis_label());
        ImPlot::SetupAxesLimits(xMin, xMax, yMin, yMax, ImPlotCond_Once);

        const double yTop = std::max(yMin, yMax);
        const double yBottom = std::min(yMin, yMax);

        ImPlot::PlotImage("##layerImage", m_mapTexture.texture_id(),
                          ImPlotPoint(xMin, yBottom), ImPlotPoint(xMax, yTop));

        proj->draw_overlay(m_imageSize);

        if (ImPlot::IsPlotHovered())
            draw_cursor_readout(*proj, ImPlot::GetPlotMousePos());

        ImPlot::EndPlot();
    }

    ImGui::TextDisabled("%s [%.1f, %.1f] %s | LOD %d | %.1f ms",
                        layer.name, m_valueMin, m_valueMax, layer.unit,
                        m_lod, m_lastGenerationMs);
}

void PlanetLayerViewer::select_planet(PlanetRepresentation planet) {
    m_selectedPlanet = planet;

    // Reload from the entity
    if (planet.entity.is_alive()) {
        if (const auto *live = planet.entity.get<PlanetTerrainParams>())
            m_selectedPlanet.terrainParams = *live;
    }

    m_dirty = false;
    m_appliedParams = m_selectedPlanet.terrainParams;
    m_needsRegen = true;
}

void PlanetLayerViewer::apply_generation_params() {
    if (!m_selectedPlanet.entity.is_alive())
        return;

    if (auto *live = m_selectedPlanet.entity.get_mut<PlanetTerrainParams>())
        *live = m_selectedPlanet.terrainParams;

    m_appliedParams = m_selectedPlanet.terrainParams;
    m_needsRegen = true;
    m_dirty = false;
}

void PlanetLayerViewer::collect_planets(flecs::world &ecs, std::vector<PlanetRepresentation> &planets) {
    // instance queries
    if (!m_planetQuery)
        m_planetQuery = ecs.query<const PlanetComp, const PlanetTerrainParams>();

    m_planetQuery.each([&](flecs::entity planet, const PlanetComp &planetComp, const PlanetTerrainParams &terrainParams) {
        planets.push_back(PlanetRepresentation{
            planet,
            planetComp,
            terrainParams});
    });
}

void PlanetLayerViewer::render(flecs::world &ecs) {
    ImGui::BeginChild("##topBar", ImVec2(0, 60), ImGuiChildFlags_Border);
    draw_controls(ecs);
    ImGui::EndChild();

    static float leftPanelWidth = 300.0f;
    ImGui::BeginChild("##leftPanel", ImVec2(leftPanelWidth, 0), ImGuiChildFlags_Border);
    draw_params();
    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::Button("##splitter", ImVec2(4.0f, ImGui::GetContentRegionAvail().y));
    if (ImGui::IsItemActive())
        leftPanelWidth += ImGui::GetIO().MouseDelta.x;
    leftPanelWidth = std::clamp(leftPanelWidth, 150.0f, 600.0f);
    ImGui::SameLine();

    ImGui::BeginChild("##mapPanel", ImVec2(0, 0), ImGuiChildFlags_Border);
    draw_map(ecs);
    ImGui::EndChild();
}
