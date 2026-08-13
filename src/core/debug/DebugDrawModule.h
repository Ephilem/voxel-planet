#pragma once

#include <flecs.h>
#include <glm/glm.hpp>
#include <vector>

#include "../math/aabb.h"
#include "utils/common/BaseModule.h"

namespace vp {

struct DebugVertex {
    glm::vec3 position;
    glm::vec4 color;
};

struct DebugDrawBuffer {
    std::vector<DebugVertex> lines;
    std::vector<DebugVertex> points;
};

class DebugDrawModule : public utils::BaseModule<DebugDrawModule> {
public:
    DebugDrawModule(flecs::world& ecs) : BaseModule(ecs) {}

private:
    static DebugDrawBuffer* s_buffer;

    void register_components(flecs::world& ecs);
    void register_systems(flecs::world& ecs);

    void register_pipelines(flecs::world& ecs) {}

    void register_submodules(flecs::world& ecs) {}

    void register_entities(flecs::world& ecs) {}

    friend class BaseModule;
};

} // namespace vp
