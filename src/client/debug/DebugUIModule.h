#pragma once

#include "core/ecs/BaseModule.h"

namespace vp::client
{
    class DebugUIModule : public core::BaseModule<DebugUIModule>
    {
    public:
        DebugUIModule(flecs::world& ecs) : BaseModule(ecs) { register_all(ecs); }

    private:
        void register_components(flecs::world& ecs);
        void register_systems(flecs::world& ecs);

        friend class BaseModule<DebugUIModule>;
    };
} // namespace vp::client
