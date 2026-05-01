//
// Created by raph on 06/04/2026.
//

#include "SpatialModule.h"

#include "spatial_components.h"
#include "spatial_utils.h"
#include "core/debug/DebugDraw.h"

void vp::SpatialModule::register_components(flecs::world &ecs) {
    ecs.component<Transform>()
            .member<float>("pos", 3, 0)
            .member<float>("rot", 3, 3 * sizeof(float))
            .member<float>("scale", 3, 6 * sizeof(float));

    ecs.component<GlobalTransform>()
            .member<float>("pos", 3, 0)
            .member<float>("rot", 3, 3 * sizeof(float))
            .member<float>("scale", 3, 6 * sizeof(float));

    ecs.component<CellCoord>()
            .member<int64_t>("x")
            .member<int64_t>("y")
            .member<int64_t>("z");

    ecs.component<LocalFloatingOrigin>()
            .member<int64_t>("cellX")
            .member<int64_t>("cellY")
            .member<int64_t>("cellZ")
            .member<float>("translation", 3, 3 * sizeof(int64_t))
            .member<double>("rotation", 4, 3 * sizeof(int64_t) + 3 * sizeof(float))
            .member<double>("transform", 16, 3 * sizeof(int64_t) + 3 * sizeof(float) + 4 * sizeof(double))
            .member<bool>("is_unchanged", 1,
                          3 * sizeof(int64_t) + 3 * sizeof(float) + 4 * sizeof(double) + sizeof(bool));

    ecs.component<Grid>()
            .member<double>("Cell Size")
            .member<LocalFloatingOrigin>("Local Origin");

    ecs.component<FloatingOrigin>();
}

void vp::SpatialModule::register_systems(flecs::world &ecs) {
    ecs.system("SpatialModule-GridNormalize")
            .with<CellCoord>() // [0]
            .with<Transform>() // [1]
            .with<const Grid>().up(flecs::ChildOf) // [2] shared
            .kind(flecs::PostUpdate)
            .run([](flecs::iter &it) {
                while (it.next()) {
                    auto cells = it.field<CellCoord>(0);
                    auto transforms = it.field<Transform>(1);
                    const Grid &grid = *it.field<const Grid>(2);

                    const float half = static_cast<float>(grid.cellSize * 0.5);

                    for (auto i: it) {
                        glm::vec3 &p = transforms[i].pos;
                        for (int axis = 0; axis < 3; ++axis) {
                            while (p[axis] > half) {
                                p[axis] -= grid.cellSize;
                                cells[i][axis]++;
                            }
                            while (p[axis] < -half) {
                                p[axis] += grid.cellSize;
                                cells[i][axis]--;
                            }
                        }
                    }
                }
            });

    // -- Floating origin tracking --
    ecs.observer<FloatingOrigin>("SpatialModule-TrackFloatingOrigin")
            .event(flecs::OnAdd)
            .each([](flecs::entity e, FloatingOrigin) {
                auto root = e.world().get_mut<SpatialRoot>();
                root->floatingOrigin = e;
            });

    ecs.observer<FloatingOrigin>("SpatialModule-ClearFloatingOrigin")
            .event(flecs::OnRemove)
            .each([](flecs::entity e, FloatingOrigin) {
                auto root = e.world().get_mut<SpatialRoot>();
                if (root->floatingOrigin == e) {
                    root->floatingOrigin = {};
                }
            });

    // -- Local floating origin calculation --
    ecs.system("SpatialModule-ComputeLocalFloatingOrigin")
            .kind(flecs::PostUpdate)
            .run([](flecs::iter &iter) {
                SpatialRoot *root = iter.world().get_mut<SpatialRoot>();
                flecs::entity floatingOrigin = root->floatingOrigin;
                flecs::entity foGridEntity = floatingOrigin.parent(); // assume that the fo is always parent of the grid
                flecs::entity worldGrid = iter.world().lookup("WorldGrid");

                CellCoord foCell = *floatingOrigin.get<CellCoord>();
                const Transform *pFoTransform = floatingOrigin.get<Transform>();
                auto foTransform = pFoTransform == nullptr ? Transform{} : *pFoTransform;

                // Step 1 - Compute foGrid Lfo: Translation from the grid origin to the lfo
                auto *g = foGridEntity.get_mut<Grid>();
                g->localOrigin.cell = foCell;
                g->localOrigin.translation = foTransform.pos;
                g->localOrigin.rotation = glm::dquat(1.0, 0.0, 0.0, 0.0);
                // Transform is a DAffine3d
                g->localOrigin.transform = glm::dmat4{
                    glm::dvec4(g->localOrigin.rotation * glm::dvec3(1.0, 0.0, 0.0), 0.0),
                    glm::dvec4(g->localOrigin.rotation * glm::dvec3(0.0, 1.0, 0.0), 0.0),
                    glm::dvec4(g->localOrigin.rotation * glm::dvec3(0.0, 0.0, 1.0), 0.0),
                    glm::dvec4(g->localOrigin.translation, 1.0)
                };

                // TODO do the propagation thing to other grids, for now we just assume there's only one grid and it's the fo grid
                flecs::entity currentGrid = foGridEntity;

                while (true) {
                    flecs::entity parentGridEntity = get_first_ancestor_grid(currentGrid);
                    if (!parentGridEntity.is_valid()) break;

                    const Grid *childGrid = currentGrid.get<Grid>();
                    const CellCoord *childCell = currentGrid.get<CellCoord>();
                    const Transform *childTransform = currentGrid.get<Transform>();
                    if (!childCell || !childTransform) break;

                    const LocalFloatingOrigin &childLfo = childGrid->localOrigin;
                    Grid *parentGrid = parentGridEntity.get_mut<Grid>();

                    const glm::dvec3 posLfoInParent =
                            glm::dvec3(*childCell) * parentGrid->cellSize + glm::dvec3(childTransform->pos)
                            + glm::dvec3(childLfo.cell) * childGrid->cellSize + glm::dvec3(childLfo.translation);

                    const int64_t cx = (int64_t) std::floor(posLfoInParent.x / parentGrid->cellSize);
                    const int64_t cy = (int64_t) std::floor(posLfoInParent.y / parentGrid->cellSize);
                    const int64_t cz = (int64_t) std::floor(posLfoInParent.z / parentGrid->cellSize);

                    parentGrid->localOrigin.cell = {cx, cy, cz};
                    parentGrid->localOrigin.translation = glm::vec3(
                        posLfoInParent - glm::dvec3(cx, cy, cz) * parentGrid->cellSize);
                    parentGrid->localOrigin.rotation = glm::dquat(1.0, 0.0, 0.0, 0.0);
                    parentGrid->localOrigin.transform = glm::dmat4{
                        glm::dvec4(parentGrid->localOrigin.rotation * glm::dvec3(1.0, 0.0, 0.0), 0.0),
                        glm::dvec4(parentGrid->localOrigin.rotation * glm::dvec3(0.0, 1.0, 0.0), 0.0),
                        glm::dvec4(parentGrid->localOrigin.rotation * glm::dvec3(0.0, 0.0, 1.0), 0.0),
                        glm::dvec4(parentGrid->localOrigin.translation, 1.0)
                    };

                    if (parentGridEntity == worldGrid) break; // stop if we reached the world grid
                    currentGrid = parentGridEntity;
                }
            });

    // -- Global Transform Calculation --
    ecs.system<const CellCoord, const Transform, GlobalTransform>("SpatialModule-PropagateHighPrecision")
            .kind(flecs::PreStore)
            .multi_threaded()
            .with<Grid>().up(flecs::ChildOf)
            .run([](flecs::iter &it) {
                while (it.next()) {
                    auto cells = it.field<const CellCoord>(0);
                    auto localTrs = it.field<const Transform>(1);
                    auto globalTrs = it.field<GlobalTransform>(2);

                    const Grid &grid = it.field<const Grid>(3)[0];

                    for (auto i: it) {
                        const LocalFloatingOrigin &lfo = grid.localOrigin;

                        const glm::i64vec3 dCell = glm::i64vec3(cells[i].x, cells[i].y, cells[i].z)
                                                   - glm::i64vec3(lfo.cell.x, lfo.cell.y, lfo.cell.z);

                        const glm::dvec3 pGrid = glm::dvec3(dCell) * (double) grid.cellSize
                                                 + glm::dvec3(localTrs[i].pos);

                        const glm::dvec3 pFo = pGrid - glm::dvec3(lfo.translation);

                        globalTrs[i].pos = glm::vec3(pFo);
                        globalTrs[i].rot = localTrs[i].rot;
                        globalTrs[i].scale = localTrs[i].scale;
                    }
                }
            });
}

void vp::SpatialModule::register_pipelines(flecs::world &ecs) {
}

void vp::SpatialModule::register_submodules(flecs::world &ecs) {
}

void vp::SpatialModule::register_entities(flecs::world &ecs) {
    ecs.set<SpatialRoot>({});
}
