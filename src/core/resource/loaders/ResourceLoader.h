#pragma once
#include <memory>

#include "core/resource/resource_type.h"

class IResourceLoader {
public:
    virtual ~IResourceLoader() = default;

    virtual std::shared_ptr<IResource> load(const std::string& name) = 0;
};

template<typename ResourceT>
class ResourceLoader : public IResourceLoader {
public:
    virtual ~ResourceLoader() = default;

    std::shared_ptr<IResource> load(const std::string &name) override {
        return load_typed(name);
    }

    virtual std::shared_ptr<ResourceT> load_typed(const std::string& name) = 0;
};
