//
// Created by raph on 29/03/2026.
//

#include "PhysicsSystem.h"

#include "physics_components.h"
#include "core/main_components.h"
#include "core/math/utils.h"
#include "core/world/ChunkManager.h"
#include "core/world/world_components.h"

void PhysicsSystem::Register(flecs::world &ecs) {
    PhysicsSystem system;
    system.init(ecs);
}

void PhysicsSystem::init(flecs::world &ecs) {
    ecs.component<Velocity>()
            .member<float>("x", 0, 0)
            .member<float>("y", 0, 0)
            .member<float>("z", 0, 0);

    ecs.system<const Velocity, Position>("Physics-ApplyVelocitySystem")
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

    ecs.system<Velocity, Position, RigidBody>("Physics-Collision")
            .kind(flecs::OnValidate)
            .each([](flecs::entity e, Velocity &vel, Position &pos, RigidBody &body) {
                if (body.noClip) {
                    // No clip, just velocity
                    pos += vel * e.world().delta_time();
                    return;
                }

                float dt = e.world().delta_time();
                auto cm = e.world().get<ChunkManager>();
                if (!cm) return;

                glm::vec3 halfExt = body.haftExtent;
                body.onGround = false;

                // For each axis (so no corner sticking)
                for (int axis = 0; axis < 3; axis++) {
                    float delta = vel[axis] * dt;
                    if (delta == 0.0f) continue;

                    glm::vec3 newPos = static_cast<glm::vec3>(pos);
                    newPos[axis] += delta;

                    // AABB after movement (if needed to cancel)
                    glm::vec3 bmin = newPos - halfExt;
                    glm::vec3 bmax = newPos + halfExt;

                    // Check all block on the final AABB, if any is solid, move back to the edge of the block and stop movement on this axis.
                    glm::ivec3 blockMin = glm::ivec3(glm::floor(bmin));
                    glm::ivec3 blockMax = glm::ivec3(glm::floor(bmax));

                    bool collided = false;
                    for (int bx = blockMin.x; bx <= blockMax.x; bx++)
                        for (int by = blockMin.y; by <= blockMax.y; by++)
                            for (int bz = blockMin.z; bz <= blockMax.z; bz++) {
                                if (!cm->is_solid({bx, by, bz})) continue;

                                // If solid, cancel movement and snap to the edge of the block
                                if (delta > 0.0f) {
                                    float wall = static_cast<float>(axis == 0 ? bx : axis == 1 ? by : bz);
                                    newPos[axis] = wall - halfExt[axis] - 0.001f;
                                } else {
                                    float wall = static_cast<float>((axis == 0 ? bx : axis == 1 ? by : bz) + 1);
                                    newPos[axis] = wall + halfExt[axis] + 0.001f;
                                }
                                vel[axis] = 0.0f;
                                collided = true;
                                break;
                            }

                    pos[axis] = newPos[axis];

                    // TODO Other collision checks (non-axis-aligned) will be handled after the full movement is applied, so we can react to the final position.
                }

                // Ground check with probe
                body.onGround = false;
                {
                    float feetY = pos.y - halfExt.y;
                    float probeY = feetY - 0.05f;

                    glm::ivec3 bMin = glm::ivec3(glm::floor(glm::vec3(pos.x - halfExt.x, probeY, pos.z - halfExt.z)));
                    glm::ivec3 bMax = glm::ivec3(glm::floor(glm::vec3(pos.x + halfExt.x, feetY,  pos.z + halfExt.z)));

                    for (int bx = bMin.x; bx <= bMax.x && !body.onGround; bx++)
                    for (int by = bMin.y; by <= bMax.y && !body.onGround; by++)
                    for (int bz = bMin.z; bz <= bMax.z && !body.onGround; bz++) {
                        if (cm->is_solid({bx, by, bz})) {
                            body.onGround = true;
                        }
                    }
                }
            });
}

void PhysicsSystem::apply_velocity(flecs::iter &it) {
    while (it.next()) {
        auto velocities = it.field<const Velocity>(0);
        auto positions = it.field<Position>(1);

        for (auto i: it) {
            positions[i].x += velocities[i].x * it.delta_time();
            positions[i].y += velocities[i].y * it.delta_time();
            positions[i].z += velocities[i].z * it.delta_time();
        }
    }
}
