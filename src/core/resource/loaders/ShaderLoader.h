#include "ResourceLoader.h"

class ShaderLoader : public ResourceLoader<ShaderResource> {
public:
    ShaderLoader() = default;
    ~ShaderLoader() override = default;

    std::shared_ptr<ShaderResource> load_typed(const std::string &name) override;
};
