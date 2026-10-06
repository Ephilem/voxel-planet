#pragma once
#include <memory>

#include "core/resource/resource_type.h"
#include "ResourceLoader.h"

namespace vp::core {
class ImageLoader : public ResourceLoader<ImageResource> {
public:
    ImageLoader() = default;
    ~ImageLoader() override = default;

    std::shared_ptr<ImageResource> load_typed(const std::string& name) override;
};
} // namespace vp::core
