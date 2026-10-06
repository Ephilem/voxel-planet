#pragma once
#include "ResourceLoader.h"

namespace vp::core {
class ShaderLoader : public ResourceLoader<ShaderResource> {
public:
    ShaderLoader() = default;
    ~ShaderLoader() override = default;

    std::shared_ptr<ShaderResource> load_typed(const std::string& name) override;
};
} // namespace vp::core
