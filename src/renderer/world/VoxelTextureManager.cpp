//
// Created by raph on 13/12/2025.
//

#include "VoxelTextureManager.h"

#include "core/GameState.h"
#include "core/log/Logger.h"
#include "renderer/Renderer.h"

VoxelTextureManager::VoxelTextureManager(VulkanBackend* backend) {
    m_backend = backend;
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

    ecs.emplace<VoxelTextureManager>(backend);
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
            .setArraySize(MAX_VOXEL_TEXTURE_SLOTS) // 1024 layers
            .setMipLevels(1)
            .setFormat(nvrhi::Format::RGBA8_UNORM)
            .setIsRenderTarget(false)
            .setIsTypeless(false)
            .setDebugName("VoxelTextureArray")
            .setDimension(nvrhi::TextureDimension::Texture2DArray)
            .setInitialState(nvrhi::ResourceStates::ShaderResource)
            .setKeepInitialState(true);
    m_textureArray = m_backend->device->createTexture(textureDesc);

    auto samplerDesc = nvrhi::SamplerDesc()
            .setAllFilters(false) // nearest neighbors
            .setAllAddressModes(nvrhi::SamplerAddressMode::Repeat)
            .setMipFilter(false); // todo generate mipmaps later
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
            .setNumMipLevels(1)
            .setBaseArraySlice(0)
            .setNumArraySlices(MAX_VOXEL_TEXTURE_SLOTS);
    auto bindingSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::Texture_SRV(0, m_textureArray,
                     nvrhi::Format::UNKNOWN,
                     subresourceRange,
                     nvrhi::TextureDimension::Texture2DArray))
            .addItem(nvrhi::BindingSetItem::Sampler(1, m_sampler));
    m_bindingSet = m_backend->device->createBindingSet(bindingSetDesc, m_bindingLayout);
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

          cmd->writeTexture(
              m_textureArray,
              slotIndex,
              0,
              textureRes->get_data(),
              rowPitch
          );
          LOG_DEBUG("VoxelTextureManager", "Uploaded texture {} to slot {}", assetIdStr, slotIndex);

          m_slots[slotIndex].uploaded = true;
      }
      cmd->setTextureState(m_textureArray, nvrhi::AllSubresources,
                           nvrhi::ResourceStates::ShaderResource);

      m_toUploadList.clear();
  }
