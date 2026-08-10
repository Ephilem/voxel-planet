//
// Created by raph on 08/08/2026.
//

#include "PlanetTileAtlas.h"

#include <algorithm>

#include "core/log/Logger.h"

namespace vp {
    PlanetTileAtlas::~PlanetTileAtlas() {
        m_texture = nullptr;
        m_sampler = nullptr;
    }

    void PlanetTileAtlas::begin_frame() {
        ++m_currentFrame;
    }

    PlanetTileAtlasKey PlanetTileAtlas::find(const PlanetTileKey &key) {
        const auto it = m_lookup.find(key);
        if (it == m_lookup.end()) return INVALID_ATLAS_SLOT;

        touch(it->second);
        return it->second;
    }

    PlanetTileAtlasKey PlanetTileAtlas::acquire_slot() {
        // Direct fetch
        for (uint32_t i = 0; i < m_capacity; ++i) {
            const uint32_t idx = (m_evictCursor + i) % m_capacity;
            if (!m_slots[idx].resident) {
                m_evictCursor = (idx + 1) % m_capacity;
                return idx;
            }
        }

        // Evict the least recently used slot that was not used this frame
        PlanetTileAtlasKey victim = m_capacity;
        uint64_t oldest = UINT64_MAX;
        for (uint32_t i = 0; i < m_capacity; ++i) {
            const uint32_t idx = (m_evictCursor + i) % m_capacity;
            const Slot &slot = m_slots[idx];

            if (slot.key.level() <= PLANET_TILE_ATLAS_PINNED_LEVEL) continue;

            if (slot.lastUsedFrame >= m_currentFrame) continue;
            if (slot.lastUsedFrame < oldest) {
                oldest = slot.lastUsedFrame;
                victim = idx;
            }
        }

        if (victim == m_capacity) return INVALID_ATLAS_SLOT; // full

        m_lookup.erase(m_slots[victim].key);
        m_slots[victim].resident = false;
        ++m_stats.evictions;
        --m_stats.resident;

        m_evictCursor = (victim + 1) % m_capacity;
        return victim;
    }

    PlanetTileAtlasKey PlanetTileAtlas::upload(nvrhi::ICommandList *cmd, const PlanetTileKey &key,
                                               const PlanetTileData &data) {
        if (cmd == nullptr || !key.valid()) {
            ++m_stats.failedUploads;
            LOG_ERROR("PlanetTileAtlas", "Invalid command list or tile key. This need to never happen!");
            return INVALID_ATLAS_SLOT;
        }

        if (data.resolution != PLANET_TILE_ATLAS_RESOLUTION) {
            LOG_ERROR("PlanetTileAtlas", "Tile resolution {} does not match atlas resolution {}",
                      data.resolution, PLANET_TILE_ATLAS_RESOLUTION);
            ++m_stats.failedUploads;
            return INVALID_ATLAS_SLOT;
        }

        const size_t expected = size_t(data.resolution) * size_t(data.resolution);
        if (data.heightmap.size() != expected) {
            LOG_ERROR("PlanetTileAtlas", "Tile heightmap has {} texels, expected {}",
                      data.heightmap.size(), expected);
            ++m_stats.failedUploads;
            return INVALID_ATLAS_SLOT;
        }

        PlanetTileAtlasKey slot;
        if (const auto it = m_lookup.find(key); it != m_lookup.end()) {
            slot = it->second;
        } else {
            slot = acquire_slot();
            if (slot == INVALID_ATLAS_SLOT) {
                LOG_WARN("PlanetTileAtlas", "Atlas full ({} slots all used this frame), tile dropped",
                         m_capacity);
                ++m_stats.failedUploads;
                return INVALID_ATLAS_SLOT;
            }

            m_lookup[key] = slot;
            m_slots[slot].key = key;
            m_slots[slot].resident = true;
            ++m_stats.resident;
        }

        const auto subresource = nvrhi::TextureSubresourceSet()
            .setBaseMipLevel(0)
            .setNumMipLevels(1)
            .setBaseArraySlice(slot)
            .setNumArraySlices(1);
        cmd->setTextureState(m_texture, subresource, nvrhi::ResourceStates::CopyDest);

        constexpr size_t rowPitch = size_t(PLANET_TILE_ATLAS_RESOLUTION) * sizeof(uint16_t);
        cmd->writeTexture(m_texture, slot, 0, data.heightmap.data(), rowPitch);

        m_slots[slot].lastUsedFrame = m_currentFrame;
        ++m_stats.uploads;

        return slot;
    }

    void PlanetTileAtlas::touch(PlanetTileAtlasKey slot) {
        if (slot >= m_capacity) return;
        if (!m_slots[slot].resident) return;

        m_slots[slot].lastUsedFrame = m_currentFrame;
    }

    void PlanetTileAtlas::finish_uploads(nvrhi::ICommandList *cmd) {
        if (cmd == nullptr) return;

        cmd->setTextureState(m_texture, nvrhi::AllSubresources, nvrhi::ResourceStates::ShaderResource);
    }

    void PlanetTileAtlas::init_gpu() {
        constexpr uint32_t requestedCapacity = PLANET_TILE_ATLAS_SIZE;

        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(m_backend->vkDevice.physical_device, &props);
        m_capacity = std::min(requestedCapacity, props.limits.maxImageArrayLayers);
        if (m_capacity < requestedCapacity) {
            LOG_WARN("PlanetTileAtlas", "Device limits the atlas to {} slices (requested {})",
                     m_capacity, requestedCapacity);
        }

        m_slots.assign(m_capacity, Slot{});
        m_lookup.reserve(m_capacity);

        auto textureDesc = nvrhi::TextureDesc()
            .setArraySize(m_capacity)
            .setWidth(PLANET_TILE_ATLAS_RESOLUTION)
            .setHeight(PLANET_TILE_ATLAS_RESOLUTION)
            .setDebugName("PlanetTileAtlas")
            .setFormat(nvrhi::Format::R16_UNORM)
            .setDimension(nvrhi::TextureDimension::Texture2DArray)
            .setMipLevels(1)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true);
        m_texture = m_backend->device->createTexture(textureDesc);

        auto samplerDesc = nvrhi::SamplerDesc()
            .setAllFilters(true)
            .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
        m_sampler = m_backend->device->createSampler(samplerDesc);

        m_stats.capacity = m_capacity;

        LOG_INFO("PlanetTileAtlas", "Allocated {} slices of {}x{} R16_UNORM ({} MiB)",
                 m_capacity, PLANET_TILE_ATLAS_RESOLUTION, PLANET_TILE_ATLAS_RESOLUTION,
                 (size_t(m_capacity) * PLANET_TILE_ATLAS_RESOLUTION * PLANET_TILE_ATLAS_RESOLUTION
                  * sizeof(uint16_t)) / (1024 * 1024));
    }
}
