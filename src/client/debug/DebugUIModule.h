#pragma once

#include "utils/common/BaseModule.h"

namespace vp {
    class DebugUIModule : public utils::BaseModule<DebugUIModule> {
    public:
        DebugUIModule(flecs::world &ecs) : BaseModule(ecs) {}

    private:
        void register_components(flecs::world &ecs);
        void register_systems(flecs::world &ecs);

        friend class BaseModule<DebugUIModule>;
    };
}
