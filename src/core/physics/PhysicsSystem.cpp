//
// Created by raph on 29/03/2026.
//

#include "PhysicsSystem.h"

#include "physics_components.h"
#include "core/main_components.h"
#include "core/debug/DebugDraw.h"
#include "core/world/spatial/spatial_components.h"

using namespace vp;

void PhysicsSystem::Register(flecs::world &ecs) {
    ecs.component<Velocity>()
            .member<float>("x", 0, 0)
            .member<float>("y", 0, 0)
            .member<float>("z", 0, 0);
    ecs.component<Gravity>()
            .member<float>("x", 0, 0)
            .member<float>("y", 0, 0)
            .member<float>("z", 0, 0);
    ecs.component<RigidBody>();

    PhysicsSystem system;
    system.init(ecs);
}

void PhysicsSystem::init(flecs::world &ecs) {
    ecs.component<Velocity>()
            .member<float>("x", 0, 0)
            .member<float>("y", 0, 0)
            .member<float>("z", 0, 0);

    ecs.system<const Velocity, Transform>("Physics-ApplyVelocitySystem")
            .kind(flecs::OnUpdate)
            .without<RigidBody>()
            .run([this](flecs::iter &it) {
                apply_velocity(it);
            });

    ecs.system<const Gravity, Velocity>("Physics-ApplyGravitySystem")
            .kind(flecs::OnUpdate)
            .each([](flecs::entity e, const Gravity &gravity, Velocity &velocity) {
                if (auto* body = e.get<RigidBody>(); body && body->onGround) {
                    if (velocity.y < 0.0f) velocity.y = 0.0f;
                    return;
                }

                float dt = e.world().delta_time();

                velocity.x += gravity.x * dt;
                velocity.y += gravity.y * dt;
                velocity.z += gravity.z * dt;
            });

    ecs.system<Velocity, Transform, RigidBody>("Physics-Collision")
            .kind(flecs::OnValidate)
            .each([](flecs::entity e, Velocity &vel, Transform &transform, RigidBody &body) {
                auto &pos = transform.pos;
                if (body.noClip) {
                    // No clip, just velocity
                    pos += vel * e.world().delta_time();
                    return;
                }

                float dt = e.world().delta_time();
                // auto cm = e.world().get<ChunkManager>();
                int* cm = nullptr;
                if (!cm) return;

                glm::vec3 halfExt = body.haftExtent;
                glm::vec3 currBmin = pos - halfExt;
                glm::vec3 currBmax = pos + halfExt;
                // DebugDrawManager::Aabb(currBmin, currBmax, glm::vec4(0.0f, 1.0f, 0.0f, 0.5f));
                body.onGround = false;

                static constexpr float kEpsilon = 0.001f;
                static constexpr float kStepHeight = 0.55f;

                // Helper: returns true if the AABB at `center` is free of solid blocks.
                auto aabbClear = [&](glm::vec3 center) -> bool {
                    glm::vec3 bmin = center - halfExt;
                    glm::vec3 bmax = center + halfExt;
                    AABB aabb{bmin, bmax};
                    glm::ivec3 bMin = glm::ivec3(glm::floor(bmin));
                    glm::ivec3 bMax = glm::ivec3(glm::floor(bmax));
                    for (int bx = bMin.x; bx <= bMax.x; bx++)
                        for (int by = bMin.y; by <= bMax.y; by++)
                            for (int bz = bMin.z; bz <= bMax.z; bz++) {
                                // AABB blockAabb = cm->get_block_info({bx, by, bz}).get_block_aabb()
                                //                  + glm::vec3(bx, by, bz);
                                // if (blockAabb.intersects(aabb)) return false;
                            }
                    return true;
                };

                float originalDeltaX = vel.x * dt;
                float originalDeltaZ = vel.z * dt;
                bool blockedX = false, blockedZ = false;
                float maxBlockTopY = pos.y - halfExt.y; // highest top-Y of any horizontally-blocking block

                // For each axis (so no corner sticking)
                for (int axis = 0; axis < 3; axis++) {
                    float delta = vel[axis] * dt;
                    if (delta == 0.0f) continue;

                    glm::vec3 newPos = pos;
                    newPos[axis] += delta;

                    // AABB after movement (if needed to cancel)
                    glm::vec3 bmin = newPos - halfExt;
                    glm::vec3 bmax = newPos + halfExt;
                    AABB aabb{bmin, bmax};
                    // DebugDrawManager::Aabb(bmin, bmax, glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));

                    // Check all block on the final AABB, if any is solid, move back to the edge of the block and stop movement on this axis.
                    glm::ivec3 blockMin = glm::ivec3(glm::floor(bmin));
                    glm::ivec3 blockMax = glm::ivec3(glm::floor(bmax));

                    bool collided = false;
                    for (int bx = blockMin.x; bx <= blockMax.x; bx++)
                        for (int by = blockMin.y; by <= blockMax.y; by++)
                            for (int bz = blockMin.z; bz <= blockMax.z; bz++) {
                                // AABB localBlockAabb = cm->get_block_info({bx, by, bz}).get_block_aabb();
                                // AABB blockAabb = localBlockAabb + glm::vec3(bx, by, bz);
                                AABB blockAabb = {glm::vec3(0), glm::vec3(0)};
                                DebugDraw::Aabb(blockAabb, glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
                                if (!blockAabb.intersects(aabb)) continue;

                                // Track highest blocking block top for step-up (horizontal axes only)
                                if (axis != 1)
                                    maxBlockTopY = glm::max(maxBlockTopY, blockAabb.max.y);

                                // If solid, cancel movement and snap to the edge of the block
                                if (delta > 0.0f) {
                                    newPos[axis] = blockAabb.min[axis] - halfExt[axis] - kEpsilon;
                                } else {
                                    newPos[axis] = blockAabb.max[axis] + halfExt[axis] + kEpsilon;
                                }
                                vel[axis] = 0.0f;
                                collided = true;
                                break;
                            }

                    if (collided && axis == 0) blockedX = true;
                    if (collided && axis == 2) blockedZ = true;

                    pos[axis] = newPos[axis];

                    // TODO Other collision checks (non-axis-aligned) will be handled after the full movement is applied, so we can react to the final position.
                }

                // Step-up: if blocked horizontally and not jumping, snap to the exact top of the blocking block.
                if ((blockedX || blockedZ) && vel.y <= 0.0f) {
                    float feetY = pos.y - halfExt.y;
                    float stepY = maxBlockTopY - feetY;

                    if (stepY > 0.0f && stepY <= kStepHeight) {
                        glm::vec3 steppedPos = pos;
                        steppedPos.y = maxBlockTopY + halfExt.y + kEpsilon;

                        if (aabbClear(steppedPos)) {
                            bool canStep = true;

                            if (blockedX) {
                                glm::vec3 testPos = steppedPos;
                                testPos.x += originalDeltaX;
                                if (aabbClear(testPos)) steppedPos.x = testPos.x;
                                else canStep = false;
                            }

                            if (canStep && blockedZ) {
                                glm::vec3 testPos = steppedPos;
                                testPos.z += originalDeltaZ;
                                if (aabbClear(testPos)) steppedPos.z = testPos.z;
                                else canStep = false;
                            }

                            if (canStep) {
                                pos = steppedPos;
                                vel.y = 0.0f;
                                if (blockedX) vel.x = originalDeltaX / dt;
                                if (blockedZ) vel.z = originalDeltaZ / dt;
                            }
                        }
                    }
                }

                // Ground check with probe
                body.onGround = false; {
                    float feetY = pos.y - halfExt.y;
                    float probeY = feetY - 0.05f;

                    glm::ivec3 bMin = glm::ivec3(glm::floor(glm::vec3(pos.x - halfExt.x, probeY, pos.z - halfExt.z)));
                    glm::ivec3 bMax = glm::ivec3(glm::floor(glm::vec3(pos.x + halfExt.x, feetY, pos.z + halfExt.z)));
                    AABB aabb = AABB{
                        glm::vec3(pos.x - halfExt.x, probeY, pos.z - halfExt.z),
                        glm::vec3(pos.x + halfExt.x, feetY, pos.z + halfExt.z)
                    };

                    // DebugDrawManager::Aabb(aabb, glm::vec4(1.0f, 0.0f, 1.0f, 0.5f));

                    for (int bx = bMin.x; bx <= bMax.x && !body.onGround; bx++)
                        for (int by = bMin.y; by <= bMax.y && !body.onGround; by++)
                            for (int bz = bMin.z; bz <= bMax.z && !body.onGround; bz++) {
                                // AABB localBlockAabb = cm->get_block_info({bx, by, bz}).get_block_aabb();
                                AABB localBlockAabb = AABB{};
                                AABB blockAabb = localBlockAabb + glm::vec3(bx, by, bz);
                                // DebugDrawManager::Aabb(blockAabb, glm::vec4(1.0f, 0.0f, 0.0f, 0.5f));
                                if (blockAabb.intersects(aabb)) {
                                    body.onGround = true;
                                }
                            }
                }
            });
}

void PhysicsSystem::apply_velocity(flecs::iter &it) {
    while (it.next()) {
        auto velocities = it.field<const Velocity>(0);
        auto transforms = it.field<Transform>(1);

        for (auto i: it) {
            transforms[i].pos.x += velocities[i].x * it.delta_time();
            transforms[i].pos.y += velocities[i].y * it.delta_time();
            transforms[i].pos.z += velocities[i].z * it.delta_time();
        }
    }
}
