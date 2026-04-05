#pragma once

#include <flecs.h>
#include <source_location>

#include "core/log/Logger.h"

namespace vp::utils {

template<typename T>
auto TypeToString() {
    auto WrapperSignature = std::string_view{ std::source_location::current().function_name() };
    auto StartPosition = WrapperSignature.find("with T =") + 9;
    auto EndPosition = WrapperSignature.find_first_of("]", StartPosition);
    return std::string(WrapperSignature.substr(StartPosition, EndPosition - StartPosition));
}

template<typename T>
class BaseModule {
public:
    BaseModule(flecs::world& ecs) {
        LOG_INFO("Module", "Registering module: {}", TypeToString<T>());

        if constexpr (requires { T::module_path(); }) {
            ecs.module<T>(T::module_path());
        } else {
            ecs.module<T>();
        }

        T* derived = static_cast<T*>(this);
        derived->register_components(ecs);
        derived->register_pipelines(ecs);
        derived->register_systems(ecs);
        derived->register_submodules(ecs);
        derived->register_entities(ecs);
    }

    ~BaseModule() {
        LOG_INFO("Module", "Unregistering module: {}", TypeToString<T>());
    }

private:
    void register_components(flecs::world& ecs) {};
    void register_systems(flecs::world& ecs) {};
    void register_pipelines(flecs::world& ecs) {};
    void register_submodules(flecs::world& ecs) {};
    void register_entities(flecs::world& ecs) {};
};

}
