#include "DebugUIModule.h"

#include "DebugUIManager.h"
#include "entities/PlayerPanel.h"
#include "LogConsole.h"
#include "performance/FpsCounter.h"
#include "world/PlanetLayerViewer.h"
#include "world/PlanetLodPanel.h"
#include "world/PlayerWorldInfo.h"

using namespace vp;

void DebugUIModule::register_components(flecs::world& ecs) {
    ecs.component<DebugUIManager>();
}

void DebugUIModule::register_systems(flecs::world& ecs) {
    DebugUIManager::Register(ecs); // TODO make this more ecs friendly by not using a singleton

    auto* debugUI = ecs.get_mut<DebugUIManager>();
    debugUI->add_panel<FpsCounter>();
    debugUI->add_panel<PlayerWorldInfo>();
    debugUI->add_panel<LogConsole>();
    debugUI->add_panel<PlayerPanel>();
    debugUI->add_panel<PlanetLayerViewer>();
    debugUI->add_panel<PlanetLodPanel>();
}
