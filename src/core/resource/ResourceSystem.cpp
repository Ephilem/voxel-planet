#include "ResourceSystem.h"

#include "loaders/ImageLoader.h"
#include "loaders/ShaderLoader.h"


ResourceSystem::ResourceSystem() {
    register_loader(ResourceType::IMAGE, std::make_unique<ImageLoader>());
    register_loader(ResourceType::SHADER, std::make_unique<ShaderLoader>());
    // TODO Other loaders
}

ResourceSystem::~ResourceSystem() {
    loaders.clear();
}

void ResourceSystem::register_loader(ResourceType type, std::unique_ptr<IResourceLoader> loader) {
    loaders[type] = std::move(loader);
}

std::shared_ptr<IResource> ResourceSystem::load(const std::string &name, ResourceType type) {
    auto it = loaders.find(type);
    if (it != loaders.end()) {
        return it->second->load(name);
    }
    return nullptr;
}