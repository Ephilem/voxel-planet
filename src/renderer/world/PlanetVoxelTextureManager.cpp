//
// Created by raph on 13/12/2025.
//

#include "PlanetVoxelTextureManager.h"

#include "core/log/Logger.h"
#include <nvrhi/vulkan.h>

namespace vp {
PlanetVoxelTextureManager::PlanetVoxelTextureManager(VulkanBackend* backend, ResourceSystem* resourceSystem) {
    m_backend = backend;
    m_resourceSystem = resourceSystem;

    m_slots.resize(MAX_VOXEL_TEXTURE_SLOTS, AssetID::Invalid);

    init_gpu();
}

PlanetVoxelTextureManager::~PlanetVoxelTextureManager() {
    m_textureArray = nullptr;
    m_bindingLayout = nullptr;
    m_bindingSet = nullptr;
    m_sampler = nullptr;

    // Mipmap generator
    m_mipmapGenerator.computeShader = nullptr;
    m_mipmapGenerator.pipeline = nullptr;
    m_mipmapGenerator.bindingLayout = nullptr;
    m_mipmapGenerator.linearSampler = nullptr;
    m_mipmapGenerator.initialized = false;
}

void PlanetVoxelTextureManager::init_gpu() {
    auto textureDesc = nvrhi::TextureDesc()
                           .setWidth(VOXEL_TEXTURE_SIZE)
                           .setHeight(VOXEL_TEXTURE_SIZE)
                           .setArraySize(MAX_VOXEL_TEXTURE_SLOTS)
                           .setMipLevels(6)
                           .setFormat(nvrhi::Format::RGBA8_UNORM)
                           .setIsRenderTarget(false)
                           .setIsTypeless(false)
                           .setDebugName("VoxelTextureArray")
                           .setIsUAV(true)
                           .setDimension(nvrhi::TextureDimension::Texture2DArray)
                           .setInitialState(nvrhi::ResourceStates::ShaderResource)
                           .setKeepInitialState(true);
    m_textureArray = m_backend->device->createTexture(textureDesc);

    auto samplerDesc = nvrhi::SamplerDesc()
                           .setAllFilters(false) // nearest neighbors
                           .setAllAddressModes(nvrhi::SamplerAddressMode::Repeat)
                           .setMipFilter(true);
    m_sampler = m_backend->device->createSampler(samplerDesc);

    auto bindingOffsets = nvrhi::VulkanBindingOffsets()
                              .setShaderResourceOffset(0)
                              .setSamplerOffset(0)
                              .setConstantBufferOffset(0)
                              .setUnorderedAccessViewOffset(384);

    auto bindingLayoutDesc = nvrhi::BindingLayoutDesc()
                                 .setVisibility(nvrhi::ShaderType::Pixel)
                                 .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0)) // binding = 0 + 0 = 0
                                 .addItem(nvrhi::BindingLayoutItem::Sampler(1))     // binding = 0 + 1 = 1
                                 .setBindingOffsets(bindingOffsets);
    m_bindingLayout = m_backend->device->createBindingLayout(bindingLayoutDesc);

    auto subresourceRange =
        nvrhi::TextureSubresourceSet().setBaseMipLevel(0).setNumMipLevels(6).setBaseArraySlice(0).setNumArraySlices(
            MAX_VOXEL_TEXTURE_SLOTS);
    auto bindingSetDesc =
        nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_textureArray, nvrhi::Format::UNKNOWN, subresourceRange,
                                                        nvrhi::TextureDimension::Texture2DArray))
            .addItem(nvrhi::BindingSetItem::Sampler(1, m_sampler));
    m_bindingSet = m_backend->device->createBindingSet(bindingSetDesc, m_bindingLayout);

    init_mipmap_generator();

    auto cmd = m_backend->device->createCommandList();
    cmd->open();
    upload_fallback_texture(cmd);
    cmd->close();
    m_backend->device->executeCommandList(cmd);
}

void PlanetVoxelTextureManager::init_mipmap_generator() {
    auto samplerDesc = nvrhi::SamplerDesc().setAllFilters(true).setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
    m_mipmapGenerator.linearSampler = m_backend->device->createSampler(samplerDesc);

    std::shared_ptr<ShaderResource> computeShaderRes =
        m_resourceSystem->load<ShaderResource>("TerrainGenerateMipmaps.comp", ResourceType::SHADER);
    auto shaderDesc = nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute);
    m_mipmapGenerator.computeShader =
        m_backend->device->createShader(shaderDesc, computeShaderRes->get_data(), computeShaderRes->get_data_size());

    auto bindingOffsets = nvrhi::VulkanBindingOffsets()
                              .setShaderResourceOffset(0)
                              .setUnorderedAccessViewOffset(1)
                              .setSamplerOffset(2)
                              .setConstantBufferOffset(10);
    auto bindingLayoutDesc = nvrhi::BindingLayoutDesc()
                                 .setVisibility(nvrhi::ShaderType::Compute)
                                 .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0)) // Source mip
                                 .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0)) // Dest mip
                                 .addItem(nvrhi::BindingLayoutItem::Sampler(0))     // Linear sampler
                                 .addItem(nvrhi::BindingLayoutItem::PushConstants(3, sizeof(uint32_t) * 4))
                                 .setBindingOffsets(bindingOffsets);
    m_mipmapGenerator.bindingLayout = m_backend->device->createBindingLayout(bindingLayoutDesc);

    auto pipelineDesc = nvrhi::ComputePipelineDesc()
                            .setComputeShader(m_mipmapGenerator.computeShader)
                            .addBindingLayout(m_mipmapGenerator.bindingLayout);
    m_mipmapGenerator.pipeline = m_backend->device->createComputePipeline(pipelineDesc);
    m_mipmapGenerator.initialized = true;
}

void PlanetVoxelTextureManager::upload_fallback_texture(nvrhi::ICommandList* cmd) {
    const auto checkerboard = generate_checkerboard_texture();

    auto subresource = nvrhi::TextureSubresourceSet()
                           .setBaseMipLevel(0)
                           .setNumMipLevels(1)
                           .setBaseArraySlice(VOXEL_TEXTURE_FALLBACK_SLOT)
                           .setNumArraySlices(1);
    cmd->setTextureState(m_textureArray, subresource, nvrhi::ResourceStates::CopyDest);

    // rowPitch = VOXEL_TEXTURE_SIZE pixels * 4 bytes per pixel (RGBA8)
    cmd->writeTexture(m_textureArray, VOXEL_TEXTURE_FALLBACK_SLOT, 0, checkerboard.data(), VOXEL_TEXTURE_SIZE * 4);

    generate_mipmaps(cmd, VOXEL_TEXTURE_FALLBACK_SLOT);

    cmd->setTextureState(m_textureArray, subresource, nvrhi::ResourceStates::ShaderResource);
}

void PlanetVoxelTextureManager::register_textures(std::span<const AssetID> textures) {
    for (const auto& textureID : textures) {
        if (textureID == AssetID::Invalid) {
            continue;
        }

        if (m_slotByTexture.contains(textureID)) {
            m_toUploadList.push_back(textureID);
            LOG_DEBUG("VoxelTextureManager", "Texture ID {} already registered, marking for reupload", textureID);
            continue;
        }

        if (m_nextFreeSlot >= MAX_VOXEL_TEXTURE_SLOTS) {
            LOG_WARN("VoxelTextureManager", "Max texture slots reached, texture {} will use the fallback", textureID);
            m_slotByTexture[textureID] = VOXEL_TEXTURE_FALLBACK_SLOT;
            continue;
        }

        const TextureSlot newSlot = static_cast<TextureSlot>(m_nextFreeSlot++);
        m_slotByTexture[textureID] = newSlot;
        m_slots[newSlot] = textureID;
        m_toUploadList.push_back(textureID);
        LOG_TRACE("VoxelTextureManager", "Registered texture ID {} to slot {}", textureID, newSlot);
    }
}

PlanetVoxelTextureManager::TextureSlot PlanetVoxelTextureManager::slot_of(const AssetID textureID) {
    auto it = m_slotByTexture.find(textureID);
    if (it != m_slotByTexture.end()) {
        return static_cast<uint16_t>(it->second);
    }

    LOG_WARN("VoxelTextureManager", "Texture ID {} not registered, returning fallback slot", textureID);
    return VOXEL_TEXTURE_FALLBACK_SLOT;
}

void PlanetVoxelTextureManager::upload_pending(nvrhi::ICommandList* cmd) {
    if (m_toUploadList.empty()) {
        return;
    }

    auto level0Subresource =
        nvrhi::TextureSubresourceSet().setBaseMipLevel(0).setNumMipLevels(1).setBaseArraySlice(0).setNumArraySlices(
            MAX_VOXEL_TEXTURE_SLOTS);
    cmd->setTextureState(m_textureArray, level0Subresource, nvrhi::ResourceStates::CopyDest);

    for (auto textureID : m_toUploadList) {
        auto it = m_slotByTexture.find(textureID);
        if (it == m_slotByTexture.end()) {
            LOG_ERROR("VoxelTextureManager", "Texture ID {} not found in slot map during upload", textureID);
            continue;
        }
        const uint32_t slotIndex = it->second;

        std::shared_ptr<ImageResource> textureRes;
        try {
            textureRes = m_resourceSystem->load<ImageResource>(textureID);
        } catch (const std::exception& e) {
            LOG_ERROR("VoxelTextureManager", "Failed to load texture ID {}: {}. Falling back to checkerboard",
                      textureID, e.what());
            it->second = VOXEL_TEXTURE_FALLBACK_SLOT;
            m_slots[slotIndex] = AssetID::Invalid;
            continue;
        }

        // The rowPitch below assumes exactly VOXEL_TEXTURE_SIZE pixels per row, so anything
        // else would be read with the wrong stride rather than merely look wrong.
        if (textureRes->width != VOXEL_TEXTURE_SIZE || textureRes->height != VOXEL_TEXTURE_SIZE) {
            LOG_ERROR("VoxelTextureManager",
                      "Texture ID {} must be exactly {}x{}, got {}x{}. Falling back to checkerboard", textureID,
                      VOXEL_TEXTURE_SIZE, VOXEL_TEXTURE_SIZE, textureRes->width, textureRes->height);
            it->second = VOXEL_TEXTURE_FALLBACK_SLOT;
            m_slots[slotIndex] = AssetID::Invalid;
            continue;
        }

        // rowPitch = VOXEL_TEXTURE_SIZE pixels * 4 bytes per pixel (RGBA8)
        cmd->writeTexture(m_textureArray, slotIndex, 0, textureRes->get_data(), VOXEL_TEXTURE_SIZE * 4);

        generate_mipmaps(cmd, slotIndex);

        LOG_TRACE("VoxelTextureManager", "Uploaded texture ID {} to slot {}", textureID, slotIndex);
    }

    m_toUploadList.clear();

    cmd->setTextureState(m_textureArray, level0Subresource, nvrhi::ResourceStates::ShaderResource);
}

void PlanetVoxelTextureManager::generate_mipmaps(nvrhi::ICommandList* cmd, TextureSlot textureSlot) {
    if (!m_mipmapGenerator.initialized) {
        LOG_ERROR("VoxelTextureManager", "Mipmap generator not initialized");
        return;
    }

    // for each level
    for (uint32_t mipLevel = 1; mipLevel < 6; mipLevel++) {
        uint32_t dstWidth = VOXEL_TEXTURE_SIZE >> mipLevel;
        uint32_t dstHeight = VOXEL_TEXTURE_SIZE >> mipLevel;

        auto srcSubresource = nvrhi::TextureSubresourceSet()
                                  .setBaseMipLevel(mipLevel - 1)
                                  .setNumMipLevels(1)
                                  .setBaseArraySlice(textureSlot)
                                  .setNumArraySlices(1);

        auto dstSubresource = nvrhi::TextureSubresourceSet()
                                  .setBaseMipLevel(mipLevel)
                                  .setNumMipLevels(1)
                                  .setBaseArraySlice(textureSlot)
                                  .setNumArraySlices(1);

        cmd->setTextureState(m_textureArray, srcSubresource, nvrhi::ResourceStates::ShaderResource);
        cmd->setTextureState(m_textureArray, dstSubresource, nvrhi::ResourceStates::UnorderedAccess);

        auto bindingSetDesc =
            nvrhi::BindingSetDesc()
                .addItem(
                    nvrhi::BindingSetItem::Texture_SRV(0, m_textureArray, nvrhi::Format::RGBA8_UNORM, srcSubresource))
                .addItem(
                    nvrhi::BindingSetItem::Texture_UAV(0, m_textureArray, nvrhi::Format::RGBA8_UNORM, dstSubresource))
                .addItem(nvrhi::BindingSetItem::Sampler(0, m_mipmapGenerator.linearSampler));
        auto bindingSet = m_backend->device->createBindingSet(bindingSetDesc, m_mipmapGenerator.bindingLayout);

        MipmapGeneratorPushConstants pushConstants = {
            .srcMipLevel = mipLevel - 1, .arraySlice = textureSlot, .dstMipWidth = dstWidth, .dstMipHeight = dstHeight};

        auto computeState = nvrhi::ComputeState().setPipeline(m_mipmapGenerator.pipeline).addBindingSet(bindingSet);

        cmd->setComputeState(computeState);
        cmd->setPushConstants(&pushConstants, sizeof(MipmapGeneratorPushConstants));

        uint32_t groupsX = (dstWidth + 7) / 8;
        uint32_t groupsY = (dstHeight + 7) / 8;
        cmd->dispatch(groupsX, groupsY, 1);
    }
}

std::array<uint32_t, VOXEL_TEXTURE_SIZE * VOXEL_TEXTURE_SIZE>
PlanetVoxelTextureManager::generate_checkerboard_texture() {
    constexpr uint32_t magenta = 0xFFFF00FF;
    constexpr uint32_t black = 0xFF000000;

    std::array<uint32_t, VOXEL_TEXTURE_SIZE * VOXEL_TEXTURE_SIZE> data{};
    for (uint32_t y = 0; y < VOXEL_TEXTURE_SIZE; y++) {
        for (uint32_t x = 0; x < VOXEL_TEXTURE_SIZE; x++) {
            const bool isLit = ((x / 4) % 2) == ((y / 4) % 2);
            data[(y * VOXEL_TEXTURE_SIZE) + x] = isLit ? magenta : black;
        }
    }
    return data;
}

} // namespace vp
