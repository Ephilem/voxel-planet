#pragma once

#include <nvrhi/nvrhi.h>

#include <unordered_map>
#include <vector>

#include "planet_rendering_components.h"
#include "renderer/vulkan/VulkanBackend.h"

#define PLANET_TILE_ATLAS_SIZE 2048u
#define PLANET_TILE_ATLAS_RESOLUTION 256u
#define PLANET_TILE_ATLAS_PINNED_LEVEL 2

namespace vp {

    /**
     * LRU cache of tile heightmaps
     */
    class PlanetTileAtlas {
    public:
        PlanetTileAtlas(VulkanBackend *backend) : m_backend(backend) {
            init_gpu();
        }
        ~PlanetTileAtlas();

        PlanetTileAtlas(const PlanetTileAtlas &) = delete;
        PlanetTileAtlas &operator=(const PlanetTileAtlas &) = delete;

        void begin_frame();

        /**
         * Looks up a tile already resident in the atlas
         * @param key Tile to look for
         * @return Its atlas slot, or INVALID_ATLAS_SLOT if the tile is not resident. The slot is
         *         touched on success, so a hit counts as a use for the current frame.
         */
        PlanetTileAtlasKey find(const PlanetTileKey &key);

        /**
         * Upload to the gpu heightmap
         *
         * @param cmd Open command list the copy is recorded into
         * @param key Information about the tile associated with the heightmap
         * @param data Heightmap data to upload
         * @return Atlas key to use for sampling the heightmap in shaders. INVALID_ATLAS_SLOT if the upload failed
         */
        PlanetTileAtlasKey upload(nvrhi::ICommandList *cmd, const PlanetTileKey &key, const PlanetTileData &data);

        /**
         * Touch to mark that slot is used in the current frame
         * @param slot slot being touched
         */
        void touch(PlanetTileAtlasKey slot);

        /**
         * Transitions the atlas back to a shader-readable state after this frame's uploads
         * @param cmd Open command list the barrier is recorded into
         */
        void finish_uploads(nvrhi::ICommandList *cmd);

        [[nodiscard]] nvrhi::ITexture *texture() const { return m_texture; }
        [[nodiscard]] nvrhi::ISampler *sampler() const { return m_sampler; }
        [[nodiscard]] uint32_t capacity() const { return m_capacity; }

        struct Stats {
            uint32_t resident = 0;
            uint32_t capacity = 0;
            uint32_t uploads = 0; // cumulative
            uint32_t evictions = 0; // cumulative
            uint32_t failedUploads = 0; // cumulative, atlas full or malformed data
            uint32_t expired = 0; // cumulative, evicted due to TTL
        };
        [[nodiscard]] const Stats &stats() const { return m_stats; }

    private:
        struct Slot {
            PlanetTileKey key;
            uint64_t lastUsedFrame = 0;
            bool resident = false;
        };

        void init_gpu();

        PlanetTileAtlasKey acquire_slot();

        VulkanBackend *m_backend = nullptr;

        nvrhi::TextureHandle m_texture;
        nvrhi::SamplerHandle m_sampler;

        uint32_t m_capacity = PLANET_TILE_ATLAS_SIZE;

        std::vector<Slot> m_slots;
        std::unordered_map<PlanetTileKey, PlanetTileAtlasKey> m_lookup;

        uint32_t m_evictCursor = 0;
        uint64_t m_currentFrame = 1;

        Stats m_stats;
    };
}
