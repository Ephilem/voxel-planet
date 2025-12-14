#pragma once
#include <unordered_map>

#include "AssetRegistry.h"
#include "loaders/ResourceLoader.h"

class ResourceSystem {
public:
    ResourceSystem(AssetRegistry* assetRegistry);
    ~ResourceSystem();

    template<typename ResourceT>
    std::shared_ptr<ResourceT> load(const std::string& name, ResourceType type) {
        auto resource = load(name, type);
        return std::dynamic_pointer_cast<ResourceT>(resource);
    }

    template<typename ResourceT>
    std::shared_ptr<ResourceT> load(const AssetID id) {
        if (!m_assetRegistry->has_asset(id)) {
            throw std::runtime_error("ResourceSystem: Asset ID not found in registry");
        }

        std::string path = m_assetRegistry->get_relative_path(id);
        auto typeOpt = m_assetRegistry->get_type(id);
        if (!typeOpt.has_value()) {
            throw std::runtime_error("ResourceSystem: Asset type not found for asset ID");
        }
        ResourceType type = typeOpt.value();

        auto resource = load<ResourceT>(path, type);
        if (!resource) {
            throw std::runtime_error("ResourceSystem: Failed to load resource '" + path + "'");
        }
        return resource;
    }

    AssetRegistry* get_asset_registry() const { return m_assetRegistry; }

private:
    std::unordered_map<ResourceType, std::unique_ptr<IResourceLoader>> loaders;
    AssetRegistry* m_assetRegistry = nullptr;

    /**
     * Register a new resource loader for a given type.
     * @param type Type of resource
     * @param loader Loader instance. The ResourceSystem takes ownership of the loader.
     */
    void register_loader(ResourceType type, std::unique_ptr<IResourceLoader> loader);

    std::shared_ptr<IResource> load(const std::string& name, ResourceType type);
};
