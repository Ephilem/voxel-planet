#include "DebugUIModule.h"

#include "DebugUIManager.h"
#include "LogConsole.h"
#include "world/PlayerWorldInfo.h"
#include "world/PlanetLayerViewer.h"
#include "world/PlanetLodPanel.h"
#include "entities/PlayerPanel.h"
#include "performance/FpsCounter.h"

using namespace vp;

void DebugUIModule::register_components(flecs::world &ecs) {
    ecs.component<DebugUIManager>();
}

void DebugUIModule::register_systems(flecs::world &ecs) {
    DebugUIManager::Register(ecs); // TODO make this more ecs friendly by not using a singleton

    auto *debugUI = ecs.get_mut<DebugUIManager>();
    debugUI->add_panel<FpsCounter>();
    debugUI->add_panel<PlayerWorldInfo>();
    debugUI->add_panel<LogConsole>();
    debugUI->add_panel<PlayerPanel>();
    debugUI->add_panel<PlanetLayerViewer>();
    debugUI->add_panel<PlanetLodPanel>();
}
