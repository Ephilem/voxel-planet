#pragma once
#include <unordered_map>

#include "loaders/ResourceLoader.h"

class ResourceSystem {
    std::unordered_map<ResourceType, std::unique_ptr<IResourceLoader>> loaders;

public:
    ResourceSystem();
    ~ResourceSystem();

    template<typename ResourceT>
    std::shared_ptr<ResourceT> load(const std::string& name, ResourceType type) {
        auto resource = load(name, type);
        return std::dynamic_pointer_cast<ResourceT>(resource);
    }

private:
    /**
     * Register a new resource loader for a given type.
     * @param type Type of resource
     * @param loader Loader instance. The ResourceSystem takes ownership of the loader.
     */
    void register_loader(ResourceType type, std::unique_ptr<IResourceLoader> loader);

    std::shared_ptr<IResource> load(const std::string& name, ResourceType type);
};
