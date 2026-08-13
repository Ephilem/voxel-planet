//
// Created by raph on 04/08/2026.
//

#include "PlanetLayerProjectionMappings.h"

#include <implot.h>

#include <cmath>
#include <cstdio>

#include "core/world/planet/planet_transform.h"

using namespace vp;

constexpr int CROSS_COLS = 4;
constexpr int CROSS_ROWS = 3;
constexpr CubemapFace CROSS_LAYOUT[CROSS_ROWS][CROSS_COLS] = {
    {FACE_UNKNOWN, FACE_POS_Y, FACE_UNKNOWN, FACE_UNKNOWN},
    {FACE_NEG_X, FACE_POS_Z, FACE_POS_X, FACE_NEG_Z},
    {FACE_UNKNOWN, FACE_NEG_Y, FACE_UNKNOWN, FACE_UNKNOWN},
};

class EquirectangularProjection final : public PlanetLayerProjectionMapping {
public:
    const char* name() const override { return "Equirectangular"; }

    glm::ivec2 image_size(int resolution) const override { return {resolution * 4, resolution * 2}; }

    void tiles(int resolution, std::vector<PlanetLayerProjectionTile>& out) const override {
        out.clear();
        out.push_back({{0, 0}, image_size(resolution), FACE_UNKNOWN});
    }

    void fill_tile_directions(const PlanetLayerProjectionTile& tile, PlanetDirectionField& out) const override {
        out.resize(tile.size.x, tile.size.y);

        for (int py = 0; py < tile.size.y; ++py) {
            const double lat = (0.5 - (py + 0.5) / tile.size.y) * M_PI;
            const double cosLat = std::cos(lat);
            const double sinLat = std::sin(lat);

            for (int px = 0; px < tile.size.x; ++px) {
                const double lon = ((px + 0.5) / tile.size.x - 0.5) * 2.0 * M_PI;

                const int i = py * tile.size.x + px;
                out.x[i] = static_cast<float>(cosLat * std::cos(lon));
                out.y[i] = static_cast<float>(sinLat);
                out.z[i] = static_cast<float>(cosLat * std::sin(lon));
            }
        }
    }

    std::string describe_pixel(int x, int y, const glm::ivec2& imageSize) const override {
        if (imageSize.x <= 0 || imageSize.y <= 0)
            return "-";

        const double lat = (0.5 - (y + 0.5) / imageSize.y) * 180.0;
        const double lon = ((x + 0.5) / imageSize.x - 0.5) * 360.0;

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%.2f%c  %.2f%c", std::abs(lat), lat >= 0.0 ? 'N' : 'S', std::abs(lon),
                      lon >= 0.0 ? 'E' : 'W');
        return buffer;
    }

    void plot_bounds(const glm::ivec2&, double& xMin, double& xMax, double& yMin, double& yMax) const override {
        xMin = -180.0;
        xMax = 180.0;
        yMin = -90.0;
        yMax = 90.0;
    }

    const char* x_axis_label() const override { return "Longitude"; }

    const char* y_axis_label() const override { return "Latitude"; }
};

class CubeCrossProjection final : public PlanetLayerProjectionMapping {
public:
    const char* name() const override { return "Cube cross"; }

    glm::ivec2 image_size(int resolution) const override { return {resolution * CROSS_COLS, resolution * CROSS_ROWS}; }

    void tiles(int resolution, std::vector<PlanetLayerProjectionTile>& out) const override {
        out.clear();
        for (int row = 0; row < CROSS_ROWS; ++row) {
            for (int col = 0; col < CROSS_COLS; ++col) {
                const CubemapFace face = CROSS_LAYOUT[row][col];
                if (face == FACE_UNKNOWN)
                    continue;

                out.push_back(
                    PlanetLayerProjectionTile{{col * resolution, row * resolution}, {resolution, resolution}, face});
            }
        }
    }

    void fill_tile_directions(const PlanetLayerProjectionTile& tile, PlanetDirectionField& out) const override {
        out.resize(tile.size.x, tile.size.y);

        for (int py = 0; py < tile.size.y; ++py) {
            const double v = 1.0 - 2.0 * (py + 0.5) / tile.size.y;

            for (int px = 0; px < tile.size.x; ++px) {
                const double u = 2.0 * (px + 0.5) / tile.size.x - 1.0;
                const glm::dvec3 d = face_uv_to_direction(tile.face, u, v);

                const int i = py * tile.size.x + px;
                out.x[i] = static_cast<float>(d.x);
                out.y[i] = static_cast<float>(d.y);
                out.z[i] = static_cast<float>(d.z);
            }
        }
    }

    std::string describe_pixel(int x, int y, const glm::ivec2& imageSize) const override {
        const int faceSize = imageSize.x / CROSS_COLS;
        if (faceSize <= 0)
            return "-";

        const int col = x / faceSize;
        const int row = y / faceSize;
        if (col < 0 || col >= CROSS_COLS || row < 0 || row >= CROSS_ROWS)
            return "-";

        const CubemapFace face = CROSS_LAYOUT[row][col];
        if (face == FACE_UNKNOWN)
            return "-";

        const double u = 2.0 * ((x % faceSize) + 0.5) / faceSize - 1.0;
        const double v = 1.0 - 2.0 * ((y % faceSize) + 0.5) / faceSize;

        char buffer[64];
        std::snprintf(buffer, sizeof(buffer), "%s  u=%.3f v=%.3f", face_name(face), u, v);
        return buffer;
    }

    void plot_bounds(const glm::ivec2& imageSize, double& xMin, double& xMax, double& yMin,
                     double& yMax) const override {
        xMin = 0.0;
        xMax = imageSize.x;
        yMin = imageSize.y;
        yMax = 0.0;
    }

    void draw_overlay(const glm::ivec2& imageSize) const override {
        const int faceSize = imageSize.x / CROSS_COLS;
        if (faceSize <= 0)
            return;

        for (int row = 0; row < CROSS_ROWS; ++row) {
            for (int col = 0; col < CROSS_COLS; ++col) {
                const CubemapFace face = CROSS_LAYOUT[row][col];
                if (face == FACE_UNKNOWN)
                    continue;

                const double x0 = col * faceSize;
                const double y0 = row * faceSize;
                const double x1 = x0 + faceSize;
                const double y1 = y0 + faceSize;

                const double bx[5] = {x0, x1, x1, x0, x0};
                const double by[5] = {y0, y0, y1, y1, y0};

                char id[32];
                std::snprintf(id, sizeof(id), "##faceBorder%d", face);
                ImPlot::PlotLine(id, bx, by, 5);

                ImPlot::PlotText(face_name(face), x0 + faceSize * 0.5, y0 + faceSize * 0.5);
            }
        }
    }
};

static const EquirectangularProjection EQUIRECT;
static const CubeCrossProjection CUBE_CROSS;

const PlanetLayerProjectionMapping& vp::equirectangular_projection() {
    return EQUIRECT;
}

const PlanetLayerProjectionMapping& vp::cube_cross_projection() {
    return CUBE_CROSS;
}

static const PlanetLayerProjectionMapping* PROJECTIONS[] = {&EQUIRECT, &CUBE_CROSS};

int vp::projection_count() {
    return 2;
}

const PlanetLayerProjectionMapping* vp::projection_at(int index) {
    if (index < 0 || index >= projection_count())
        return nullptr;
    return PROJECTIONS[index];
}