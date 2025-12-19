//
// Created by raph on 13/12/2025.
//

#include "VoxelTextureManager.h"

#include "../../../cmake-build-debug/_deps/nvrhi-src/src/vulkan/vulkan-backend.h"
#include "core/GameState.h"
#include "core/log/Logger.h"
#include "renderer/Renderer.h"

VoxelTextureManager::VoxelTextureManager(VulkanBackend* backend, ResourceSystem* resourceSystem) {
    m_backend = backend;
    m_resourceSystem = resourceSystem;

    init();
}

VoxelTextureManager::~VoxelTextureManager() {
    release_resources();
}

uint16_t VoxelTextureManager::request_texture_slot(const AssetID &textureID) {
    auto it = m_textures.find(textureID);
    if (it != m_textures.end()) {
        return it->second;
    }

    for (uint32_t i = 0; i < m_slots.size(); ++i) {
        if (m_slots[i].textureID == 0) {
            m_textures[textureID] = i;
            m_slots[i].textureID = textureID;
            m_slots[i].uploaded = false;
            m_toUploadList.push_back(textureID);
            LOG_DEBUG("VoxelTextureManager", "Assigned texture ID {} to slot {}", textureID, i);
            return i;
        }
    }

    LOG_WARN("VoxelTextureManager", "No available texture slots, returning slot 0");
    return 0;
}

void VoxelTextureManager::Register(flecs::world &ecs) {
    auto* backend = ecs.get_mut<Renderer>()->backend.get();
    auto* gameState = ecs.get_mut<GameState>();

    ecs.emplace<VoxelTextureManager>(backend, gameState->resourceSystem.get());
    auto* textureManager = ecs.get_mut<VoxelTextureManager>();

    ecs.system<Renderer>("UploadVoxelTexturesSystem")
        .kind(flecs::PreStore)
        .each([textureManager](flecs::entity e, Renderer& renderer) {
            auto* gameState = e.world().get<GameState>();
            textureManager->upload_pending_textures_system(renderer, gameState->resourceSystem.get());
        });
}

void VoxelTextureManager::init() {
    m_slots.resize(MAX_VOXEL_TEXTURE_SLOTS);

    auto textureDesc = nvrhi::TextureDesc()
            .setWidth(32)
            .setHeight(32)
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
            .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0))  // binding = 0 + 0 = 0
            .addItem(nvrhi::BindingLayoutItem::Sampler(1))      // binding = 0 + 1 = 1
            .setBindingOffsets(bindingOffsets);
    m_bindingLayout = m_backend->device->createBindingLayout(bindingLayoutDesc);

    auto subresourceRange = nvrhi::TextureSubresourceSet()
            .setBaseMipLevel(0)
            .setNumMipLevels(6)
            .setBaseArraySlice(0)
            .setNumArraySlices(MAX_VOXEL_TEXTURE_SLOTS);
    auto bindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_textureArray,
                     nvrhi::Format::UNKNOWN,
                     subresourceRange,
                     nvrhi::TextureDimension::Texture2DArray))
            .addItem(nvrhi::BindingSetItem::Sampler(1, m_sampler));
    m_bindingSet = m_backend->device->createBindingSet(bindingSetDesc, m_bindingLayout);

    init_mipmap_generator();
}

void VoxelTextureManager::init_mipmap_generator() {
    auto samplerDesc = nvrhi::SamplerDesc()
        .setAllFilters(true)
        .setAllAddressModes(nvrhi::SamplerAddressMode::Clamp);
    m_mipmapGenerator.linearSampler = m_backend->device->createSampler(samplerDesc);

    std::shared_ptr<ShaderResource> computeShaderRes = m_resourceSystem->load<ShaderResource>("TerrainGenerateMipmaps.comp", ResourceType::SHADER);
    auto shaderDesc = nvrhi::ShaderDesc()
        .setShaderType(nvrhi::ShaderType::Compute);
    m_mipmapGenerator.computeShader = m_backend->device->createShader(
        shaderDesc,
        computeShaderRes->get_data(),
        computeShaderRes->get_data_size());

    auto bindingOffsets = nvrhi::VulkanBindingOffsets()
        .setShaderResourceOffset(0)
        .setUnorderedAccessViewOffset(1)
        .setSamplerOffset(2)
        .setConstantBufferOffset(10);
    auto bindingLayoutDesc = nvrhi::BindingLayoutDesc()
        .setVisibility(nvrhi::ShaderType::Compute)
        .addItem(nvrhi::BindingLayoutItem::Texture_SRV(0)) // Source mip
        .addItem(nvrhi::BindingLayoutItem::Texture_UAV(0)) // Dest mip
        .addItem(nvrhi::BindingLayoutItem::Sampler(0)) // Linear sampler
        .addItem(nvrhi::BindingLayoutItem::PushConstants(3, sizeof(uint32_t) * 4))
        .setBindingOffsets(bindingOffsets);
    m_mipmapGenerator.bindingLayout = m_backend->device->createBindingLayout(bindingLayoutDesc);

    auto pipelineDesc = nvrhi::ComputePipelineDesc()
        .setComputeShader(m_mipmapGenerator.computeShader)
        .addBindingLayout(m_mipmapGenerator.bindingLayout);
    m_mipmapGenerator.pipeline = m_backend->device->createComputePipeline(pipelineDesc);
    m_mipmapGenerator.initialized = true;
}

void VoxelTextureManager::generate_mipmaps(nvrhi::CommandListHandle cmd, uint32_t textureSlot) {
    if (!m_mipmapGenerator.initialized) {
        LOG_ERROR("VoxelTextureManager", "Mipmap generator not initialized");
        return;
    }

    // for each level
    for (uint32_t mipLevel = 1; mipLevel < 6; mipLevel++) {
        uint32_t srcWidth = 32 >> (mipLevel - 1);
        uint32_t srcHeight = 32 >> (mipLevel - 1);
        uint32_t dstWidth = 32 >> mipLevel;
        uint32_t dstHeight = 32 >> mipLevel;

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

        cmd->setTextureState(m_textureArray, srcSubresource,
                             nvrhi::ResourceStates::ShaderResource);
        cmd->setTextureState(m_textureArray, dstSubresource,
                             nvrhi::ResourceStates::UnorderedAccess);

        auto bindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_textureArray,
                     nvrhi::Format::RGBA8_UNORM, srcSubresource))
            .addItem(nvrhi::BindingSetItem::Texture_UAV(0, m_textureArray,
                     nvrhi::Format::RGBA8_UNORM, dstSubresource))
            .addItem(nvrhi::BindingSetItem::Sampler(0, m_mipmapGenerator.linearSampler));
        auto bindingSet = m_backend->device->createBindingSet(bindingSetDesc, m_mipmapGenerator.bindingLayout);

        MipmapGeneratorPushConstants pushConstants = {
            .srcMipLevel = mipLevel - 1,
            .arraySlice = textureSlot,
            .dstMipWidth = dstWidth,
            .dstMipHeight = dstHeight
        };

        auto computeState = nvrhi::ComputeState()
            .setPipeline(m_mipmapGenerator.pipeline)
            .addBindingSet(bindingSet);

        cmd->setComputeState(computeState);
        cmd->setPushConstants(&pushConstants, sizeof(MipmapGeneratorPushConstants));

        uint32_t groupsX = (dstWidth + 7) / 8;
        uint32_t groupsY = (dstHeight + 7) / 8;
        cmd->dispatch(groupsX, groupsY, 1);
    }

    auto sliceSubresource = nvrhi::TextureSubresourceSet()
        .setBaseMipLevel(0)
        .setNumMipLevels(6)
        .setBaseArraySlice(textureSlot)
        .setNumArraySlices(1);

    cmd->setTextureState(m_textureArray, sliceSubresource,
                         nvrhi::ResourceStates::ShaderResource);
}

void VoxelTextureManager::upload_pending_textures_system(Renderer &renderer, ResourceSystem* resourceSys) {
      if (m_toUploadList.empty()) return;

      auto& cmd = renderer.frameContext.commandList;

      cmd->setTextureState(m_textureArray, nvrhi::AllSubresources,
                           nvrhi::ResourceStates::CopyDest);

      constexpr size_t rowPitch = 32 * 4; // RGBA8: width * 4 bytes

      for (const AssetID assetId : m_toUploadList) {
          std::string assetIdStr = resourceSys->get_asset_registry()->get_debug_name(assetId);
          auto it = m_textures.find(assetId);
          if (it == m_textures.end()) {
              LOG_ERROR("VoxelTextureManager", "Texture ID {} not found in textures map during upload", assetIdStr);
              continue;
          }
          uint32_t slotIndex = it->second;

          std::shared_ptr<ImageResource> textureRes;
          try {
              textureRes = resourceSys->load<ImageResource>(assetId);
          } catch (const std::exception &e) {
              LOG_ERROR("VoxelTextureManager", "Failed to load texture ID {}: {}", assetIdStr, e.what());
              continue;
          }

          if (textureRes->width < 32 || textureRes->height < 32) {
              LOG_WARN("VoxelTextureManager", "Texture ID {} has invalid size ({}x{}), expected 32x32",
                       assetIdStr, textureRes->width, textureRes->height);
              continue;
          }

          // transition to copydest for uploading mip level 0
          auto level0Subresource = nvrhi::TextureSubresourceSet()
              .setBaseMipLevel(0)
              .setNumMipLevels(1)
              .setBaseArraySlice(slotIndex)
              .setNumArraySlices(1);
          cmd->setTextureState(m_textureArray, level0Subresource,
                               nvrhi::ResourceStates::CopyDest);

          // upload mip level 0
          constexpr size_t rowPitch = 32 * 4;
          cmd->writeTexture(m_textureArray, slotIndex, 0,
                           textureRes->get_data(), rowPitch);
          LOG_DEBUG("VoxelTextureManager", "Uploaded texture {} to slot {}",
                    assetIdStr, slotIndex);

          generate_mipmaps(cmd, slotIndex);

          LOG_DEBUG("VoxelTextureManager", "Generated mipmaps for slot {}", slotIndex);

          m_slots[slotIndex].uploaded = true;
      }
      cmd->setTextureState(m_textureArray, nvrhi::AllSubresources,
                           nvrhi::ResourceStates::ShaderResource);

      m_toUploadList.clear();
  }
