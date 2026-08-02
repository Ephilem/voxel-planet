//
// Created by raph on 03/08/2026.
//

#include "PlanetTerrainSampler.h"

#include <algorithm>
#include <cmath>
#include <utility>

using namespace vp;

constexpr float GAIN = 0.5f;
constexpr float LACUNARITY = 2.0f;

constexpr int RIDGED_SEED_OFFSET = 100;

constexpr float LAND_MASK_LOW = -200.0f;
constexpr float LAND_MASK_HIGH = 200.0f;

PlanetTerrainSampler::PlanetTerrainSampler(PlanetTerrainParams params)
    : m_params(std::move(params)), m_noise(FastNoise::New<FastNoise::Simplex>()) {
}

// Les octaves sont sommées à la main plutôt que via FastNoise::FractalFBm :
// SetOctaveCount() mute le node partagé (et recalcule mFractalBounding), donc
// il est inutilisable pour un nombre d'octaves variable par appel.
float PlanetTerrainSampler::fbm(const glm::vec3 &position, float freq, int octave) const {
    const int baseSeed = m_params.seed;

    float sum = 0.0f;
    float amplitude = 1.0f;
    float norm = 0.0f;
    float f = freq;

    for (int i = 0; i < octave; ++i) {
        sum += amplitude * m_noise->GenSingle3D(position.x * f, position.y * f, position.z * f, baseSeed + i);
        norm += amplitude;
        amplitude *= GAIN;
        f *= LACUNARITY;
    }

    return norm > 0.0f ? sum / norm : 0.0f;
}

float PlanetTerrainSampler::ridged(const glm::vec3 &position, float freq, int octave) const {
    const int baseSeed = m_params.seed + RIDGED_SEED_OFFSET;

    float sum = 0.0f;
    float amplitude = 1.0f;
    float norm = 0.0f;
    float f = freq;

    for (int i = 0; i < octave; ++i) {
        const float n = m_noise->GenSingle3D(position.x * f, position.y * f, position.z * f, baseSeed + i);
        sum += amplitude * (1.0f - std::abs(n)); // crêtes
        norm += amplitude;
        amplitude *= GAIN;
        f *= LACUNARITY;
    }

    return norm > 0.0f ? (sum / norm) * 2.0f - 1.0f : 0.0f; // [-1, 1]
}

// Règle n°2 : band-limiting. Une octave n'est incluse que si sa longueur
// d'onde dépasse 2 x la taille de cellule du LOD courant (Nyquist).
// Sans ça, popping permanent au LOD switch.
int PlanetTerrainSampler::octave_for_lod(int lod) const {
    return std::clamp(3 + lod, 1, m_params.maxOctave);
}

float PlanetTerrainSampler::sample_height(const glm::dvec3 &direction, int lod) const {
    const glm::vec3 d = glm::vec3(direction);

    const int octave = octave_for_lod(lod);

    float h = fbm(d, m_params.continentFrequency, m_params.continentOctave) * m_params.continentAmplitude;

    // montagnes : ridged, atténuées en mer pour ne pas créer d'îles-pics.
    const float landMask = glm::smoothstep(LAND_MASK_LOW, LAND_MASK_HIGH, h);
    h += ridged(d, m_params.mountainFrequency, octave) * m_params.mountainAmplitude * landMask;

    return h - m_params.seaLevel;
}

void PlanetTerrainSampler::BatchScratch::resize(int count) {
    // Le padding couvre l'overread SIMD de GenPositionArray3D.
    const size_t padded = static_cast<size_t>(count) + SIMD_PADDING;
    if (sx.size() >= padded)
        return;

    sx.resize(padded);
    sy.resize(padded);
    sz.resize(padded);
    noise.resize(padded);
    continents.resize(padded);
    mountains.resize(padded);
}

// GenSingle3D calcule un vecteur SIMD complet puis n'en garde qu'une voie
// (Generator.inl:337). En passant par GenPositionArray3D on remplit toutes les
// voies : la boucle des octaves passe donc a l'exterieur, un appel par octave.
void PlanetTerrainSampler::fbm_batch(const float *px, const float *py, const float *pz,
                                     int count, float freq, int octave,
                                     float *out, BatchScratch &scratch) const {
    std::fill(out, out + count, 0.0f);

    const int baseSeed = m_params.seed;
    float amplitude = 1.0f;
    float norm = 0.0f;
    float f = freq;

    for (int i = 0; i < octave; ++i) {
        for (int k = 0; k < count; ++k) {
            scratch.sx[k] = px[k] * f;
            scratch.sy[k] = py[k] * f;
            scratch.sz[k] = pz[k] * f;
        }

        m_noise->GenPositionArray3D(scratch.noise.data(), count,
                                    scratch.sx.data(), scratch.sy.data(), scratch.sz.data(),
                                    0.0f, 0.0f, 0.0f, baseSeed + i);

        for (int k = 0; k < count; ++k)
            out[k] += amplitude * scratch.noise[k];

        norm += amplitude;
        amplitude *= GAIN;
        f *= LACUNARITY;
    }

    if (norm > 0.0f) {
        const float inv = 1.0f / norm;
        for (int k = 0; k < count; ++k)
            out[k] *= inv;
    }
}

void PlanetTerrainSampler::ridged_batch(const float *px, const float *py, const float *pz,
                                        int count, float freq, int octave,
                                        float *out, BatchScratch &scratch) const {
    std::fill(out, out + count, 0.0f);

    const int baseSeed = m_params.seed + RIDGED_SEED_OFFSET;
    float amplitude = 1.0f;
    float norm = 0.0f;
    float f = freq;

    for (int i = 0; i < octave; ++i) {
        for (int k = 0; k < count; ++k) {
            scratch.sx[k] = px[k] * f;
            scratch.sy[k] = py[k] * f;
            scratch.sz[k] = pz[k] * f;
        }

        m_noise->GenPositionArray3D(scratch.noise.data(), count,
                                    scratch.sx.data(), scratch.sy.data(), scratch.sz.data(),
                                    0.0f, 0.0f, 0.0f, baseSeed + i);

        for (int k = 0; k < count; ++k)
            out[k] += amplitude * (1.0f - std::abs(scratch.noise[k])); // ridge

        norm += amplitude;
        amplitude *= GAIN;
        f *= LACUNARITY;
    }

    if (norm > 0.0f) {
        const float inv = 1.0f / norm;
        for (int k = 0; k < count; ++k)
            out[k] = (out[k] * inv) * 2.0f - 1.0f; // [-1, 1]
    }
}

void PlanetTerrainSampler::sample_height_batch(const float *dirX, const float *dirY, const float *dirZ,
                                               int count, int lod,
                                               float *outHeights, BatchScratch &scratch) const {
    if (count <= 0)
        return;

    scratch.resize(count);

    const int octave = octave_for_lod(lod);

    // Le landMask depend du resultat des continents : impossible de tout fusionner
    // en une passe, mais deux passes batch suffisent (une par type de bruit).
    fbm_batch(dirX, dirY, dirZ, count, m_params.continentFrequency, m_params.continentOctave,
              scratch.continents.data(), scratch);
    ridged_batch(dirX, dirY, dirZ, count, m_params.mountainFrequency, octave,
                 scratch.mountains.data(), scratch);

    for (int k = 0; k < count; ++k) {
        float h = scratch.continents[k] * m_params.continentAmplitude;
        const float landMask = glm::smoothstep(LAND_MASK_LOW, LAND_MASK_HIGH, h);
        h += scratch.mountains[k] * m_params.mountainAmplitude * landMask;
        outHeights[k] = h - m_params.seaLevel;
    }
}
