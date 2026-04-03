#include "SceneRenderModule.h"

#include "client/render/camera/Camera3dModule.h"
#include "client/render/planet/PlanetRenderModule.h"
#include "client/render/render_settings.h"


namespace vp::client {
void SceneRenderModule::register_components(flecs::world& ecs) {
    // Before the submodules: their queries read it as a singleton
    ecs.component<RenderingPreferences>().add(flecs::Singleton);
}

void SceneRenderModule::register_systems(flecs::world& ecs) {
}

void SceneRenderModule::register_pipelines(flecs::world& ecs) {
}

void SceneRenderModule::register_submodules(flecs::world& ecs) {
    ecs.import<Camera3dModule>();
    ecs.import<PlanetRenderModule>();
}

void SceneRenderModule::register_entities(flecs::world& ecs) {
    ecs.emplace<RenderingPreferences>();
}
} // namespace vp::client
