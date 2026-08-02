#pragma once
#include <FastNoise/Generators/Simplex.h>

#include "core/world/planet/planet_components.h"

#include <glm/glm.hpp>
#include <vector>

#include <FastNoise/FastNoise.h>

namespace vp {

    class PlanetTerrainSampler {
    public:
        static constexpr int SIMD_PADDING = 16;

        explicit PlanetTerrainSampler(PlanetTerrainParams params = {});

        PlanetTerrainSampler(const PlanetTerrainSampler&) = delete;
        PlanetTerrainSampler& operator=(const PlanetTerrainSampler&) = delete;
        PlanetTerrainSampler(PlanetTerrainSampler&&) = default;
        PlanetTerrainSampler& operator=(PlanetTerrainSampler&&) = default;

        // Reusable buffers between calls
        struct BatchScratch {
            std::vector<float> sx, sy, sz;
            std::vector<float> noise;
            std::vector<float> continents;
            std::vector<float> mountains;

            void resize(int count);
        };


        float sample_height(const glm::dvec3& direction, int lod) const;
        void sample_height_batch(const float* dirX,
                                 const float* dirY,
                                 const float* dirZ,
                                 int count,
                                 int lod,

                                 float* outHeights, BatchScratch& scratch) const;

        const PlanetTerrainParams& params() const { return m_params; }

    private:
        float fbm(const glm::vec3& position, float freq, int octave) const;
        float ridged(const glm::vec3& position, float freq, int octave) const;

        int octave_for_lod(int lod) const;

        void fbm_batch(const float* px, const float* py, const float* pz,
                       int count, float freq, int octave,
                       float* out, BatchScratch& scratch) const;
        void ridged_batch(const float* px, const float* py, const float* pz,
                          int count, float freq, int octave,
                          float* out, BatchScratch& scratch) const;

        PlanetTerrainParams m_params;
        FastNoise::SmartNode<FastNoise::Simplex> m_noise;
    };


    /**
     * One scratch per thread.
     */
    static PlanetTerrainSampler::BatchScratch &thread_scratch() {
        thread_local PlanetTerrainSampler::BatchScratch scratch;
        return scratch;
    }
}
