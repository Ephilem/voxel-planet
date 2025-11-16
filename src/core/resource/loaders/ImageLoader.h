#pragma once
#include <memory>

#include "ResourceLoader.h"
#include "core/resource/resource_type.h"

class ImageLoader : public ResourceLoader<ImageResource> {
public:
    ImageLoader() = default;
    ~ImageLoader() override = default;

    std::shared_ptr<ImageResource> load_typed(const std::string &name) override;
};
