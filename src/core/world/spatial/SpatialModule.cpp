//
// Created by raph on 06/04/2026.
//

#include "SpatialModule.h"

#include "core/debug/DebugDraw.h"
#include "spatial_components.h"
#include "spatial_utils.h"

void vp::SpatialModule::register_components(flecs::world& ecs) {
    ecs.component<Transform>()
        .member<float>("pos", 3, offsetof(Transform, pos))
        .member<float>("rot", 4, offsetof(Transform, rot))
        .member<float>("scale", 3, offsetof(Transform, scale));

    ecs.component<GlobalTransform>()
        .member<double>("pos", 3, offsetof(GlobalTransform, pos))
        .member<float>("rot", 4, offsetof(GlobalTransform, rot))
        .member<float>("scale", 3, offsetof(GlobalTransform, scale));

    ecs.component<CellCoord>().member<int64_t>("x").member<int64_t>("y").member<int64_t>("z");

    ecs.component<LocalFloatingOrigin>()
        .member<int64_t>("cell", 3, offsetof(LocalFloatingOrigin, cell))
        .member<float>("translation", 3, offsetof(LocalFloatingOrigin, translation))
        .member<double>("rotation", 4, offsetof(LocalFloatingOrigin, rotation));

    ecs.component<Grid>()
        .member<double>("Cell Size", 1, offsetof(Grid, cellSize))
        .member<LocalFloatingOrigin>("Local Origin", 1, offsetof(Grid, localOrigin));

    ecs.component<FloatingOrigin>();

    ecs.component<LookAngles>().member<float>("yaw").member<float>("pitch").member<float>("roll");
}

void vp::SpatialModule::register_systems(flecs::world& ecs) {
    ecs.system("SpatialModule-GridNormalize")
        .with<CellCoord>() // [0]
        .with<Transform>() // [1]
        .with<const Grid>()
        .up(flecs::ChildOf) // [2] shared
        .kind(flecs::PostUpdate)
        .run([](flecs::iter& it) {
            while (it.next()) {
                auto cells = it.field<CellCoord>(0);
                auto transforms = it.field<Transform>(1);
                const Grid& grid = *it.field<const Grid>(2);

                for (auto i : it) {
                    glm::vec3& p = transforms[i].pos;
                    const glm::dvec3 shift = glm::round(glm::dvec3(p) / grid.cellSize);
                    if (shift != glm::dvec3(0.0)) {
                        cells[i] += glm::i64vec3(shift);
                        p = glm::vec3(glm::dvec3(p) - shift * grid.cellSize);
                    }
                }
            }
        });

    // -- Floating origin tracking --
    ecs.observer<FloatingOrigin>("SpatialModule-TrackFloatingOrigin")
        .event(flecs::OnAdd)
        .each([](flecs::entity e, FloatingOrigin) {
            auto root = e.world().try_get_mut<SpatialRoot>();
            root->floatingOrigin = e;
        });

    // -- Local floating origin calculation --
    ecs.system("SpatialModule-ComputeLocalFloatingOrigin").kind(flecs::PostUpdate).run([](flecs::iter& iter) {
        SpatialRoot* root = iter.world().try_get_mut<SpatialRoot>();
        flecs::entity floatingOrigin = root->floatingOrigin;
        if (!floatingOrigin.is_alive() || !floatingOrigin.has<CellCoord>()) {
            return;
        }
        flecs::entity foGridEntity = floatingOrigin.parent(); // assume that the fo is always parent of the grid
        flecs::entity worldGrid = iter.world().lookup("WorldGrid");

        CellCoord foCell = *floatingOrigin.try_get<CellCoord>();
        const Transform* pFoTransform = floatingOrigin.try_get<Transform>();
        auto foTransform = pFoTransform == nullptr ? Transform{} : *pFoTransform;

        // Step 1 - Compute foGrid Lfo: Translation from the grid origin to the lfo
        auto* g = foGridEntity.try_get_mut<Grid>();
        g->localOrigin.cell = foCell;
        g->localOrigin.translation = foTransform.pos;
        g->localOrigin.rotation = glm::dquat(1.0, 0.0, 0.0, 0.0);

        flecs::entity currentGrid = foGridEntity;

        while (true) {
            flecs::entity parentGridEntity = get_first_ancestor_grid(currentGrid);
            if (!parentGridEntity.is_valid())
                break;

            const Grid* childGrid = currentGrid.try_get<Grid>();
            const CellCoord* childCell = currentGrid.try_get<CellCoord>();
            const Transform* childTransform = currentGrid.try_get<Transform>();
            if (!childCell || !childTransform)
                break;

            const LocalFloatingOrigin& childLfo = childGrid->localOrigin;
            Grid* parentGrid = parentGridEntity.try_get_mut<Grid>();

            const glm::dvec3 pInChild =
                glm::dvec3(childLfo.cell) * childGrid->cellSize + glm::dvec3(childLfo.translation);

            const glm::dvec3 childOriginInParent =
                glm::dvec3(*childCell) * parentGrid->cellSize + glm::dvec3(childTransform->pos);

            const glm::dvec3 posLfoInParent = childOriginInParent + pInChild;

            const int64_t cx = (int64_t)std::floor(posLfoInParent.x / parentGrid->cellSize);
            const int64_t cy = (int64_t)std::floor(posLfoInParent.y / parentGrid->cellSize);
            const int64_t cz = (int64_t)std::floor(posLfoInParent.z / parentGrid->cellSize);

            parentGrid->localOrigin.cell = {cx, cy, cz};
            parentGrid->localOrigin.translation =
                glm::vec3(posLfoInParent - glm::dvec3(cx, cy, cz) * parentGrid->cellSize);
            parentGrid->localOrigin.rotation = childLfo.rotation;

            if (parentGridEntity == worldGrid)
                break; // stop if we reached the world grid
            currentGrid = parentGridEntity;
        }
    });

    // -- Global Transform Calculation --
    ecs.system<const CellCoord, const Transform, GlobalTransform>("SpatialModule-PropagateHighPrecision")
        .kind(flecs::PreStore)
        .multi_threaded()
        .with<Grid>()
        .up(flecs::ChildOf)
        .run([](flecs::iter& it) {
            while (it.next()) {
                auto cells = it.field<const CellCoord>(0);
                auto localTrs = it.field<const Transform>(1);
                auto globalTrs = it.field<GlobalTransform>(2);

                const Grid& grid = it.field<const Grid>(3)[0];

                for (auto i : it) {
                    const LocalFloatingOrigin& lfo = grid.localOrigin;

                    const glm::i64vec3 dCell = glm::i64vec3(cells[i].x, cells[i].y, cells[i].z) -
                                               glm::i64vec3(lfo.cell.x, lfo.cell.y, lfo.cell.z);

                    const glm::dvec3 pGrid = glm::dvec3(dCell) * (double)grid.cellSize + glm::dvec3(localTrs[i].pos);

                    globalTrs[i].pos = glm::conjugate(lfo.rotation) * (pGrid - glm::dvec3(lfo.translation));
                    globalTrs[i].rot = glm::quat(glm::conjugate(lfo.rotation) * glm::dquat(localTrs[i].rot));
                    globalTrs[i].scale = localTrs[i].scale;
                }
            }
        });
}

void vp::SpatialModule::register_pipelines(flecs::world& ecs) {}

void vp::SpatialModule::register_submodules(flecs::world& ecs) {}

void vp::SpatialModule::register_entities(flecs::world& ecs) {
    ecs.component<SpatialRoot>().add(flecs::Singleton);
    ecs.set<SpatialRoot>({});
}
