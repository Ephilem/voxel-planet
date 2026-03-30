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
            .each([](flecs::entity e,const Gravity& gravity, Velocity& velocity) {
                float dt = e.world().delta_time();

                velocity.x += gravity.x * dt;
                velocity.y += gravity.y * dt;
                velocity.z += gravity.z * dt;
            });

    ecs.system<Velocity, Position, RigidBody>("Physics-TerrainCollisionSystem")
    .kind(flecs::OnValidate)
    .each([](flecs::entity e, Velocity& vel, Position& pos, RigidBody& body) {
        float dt = e.world().delta_time();
        auto cm = e.world().get<ChunkManager>();
        if (!cm) return;

        constexpr float SKIN = 0.001f;
        AABB box = body.box();
        body.onGround = false;

        // TODO: Support other collision sources
        // Sweep each axis independently: Y first (gravity), then X, Z
        for (int axis : {1, 0, 2}) {
            float delta = (&vel.x)[axis] * dt;
            if (delta == 0.0f) continue;

            glm::vec3 p(pos.x, pos.y, pos.z);
            glm::vec3 wmin = p + box.min;
            glm::vec3 wmax = p + box.max;

            // Expand the AABB in the direction of movement to find candidate blocks
            glm::vec3 scan_min = wmin;
            glm::vec3 scan_max = wmax;
            if (delta > 0) scan_max[axis] += delta;
            else           scan_min[axis] += delta;

            // Shrink non-sweep axes by SKIN to avoid false positives with adjacent blocks
            for (int a = 0; a < 3; a++) {
                if (a != axis) {
                    scan_min[a] += SKIN;
                    scan_max[a] -= SKIN;
                }
            }

            // Integer block range to check
            glm::ivec3 bmin(glm::floor(scan_min));
            glm::ivec3 bmax(glm::floor(scan_max));

            // Find the closest blocking surface
            float allowed = delta;

            for (int x = bmin.x; x <= bmax.x; x++)
            for (int y = bmin.y; y <= bmax.y; y++)
            for (int z = bmin.z; z <= bmax.z; z++) {
                if (!cm->is_solid({x, y, z})) continue;

                float block[3] = {(float)x, (float)y, (float)z};

                if (delta > 0) {
                    // Moving +, block face is at block[axis]
                    float dist = block[axis] - wmax[axis];
                    allowed = std::min(allowed, std::max(dist - SKIN, 0.0f));
                } else {
                    // Moving -, block face is at block[axis]+1
                    float dist = block[axis] + 1.0f - wmin[axis];
                    allowed = std::max(allowed, std::min(dist + SKIN, 0.0f));
                }
            }

            // Apply clamped movement
            (&pos.x)[axis] += allowed;

            // If blocked, zero velocity on this axis
            if (std::abs(allowed) < std::abs(delta) - SKIN) {
                (&vel.x)[axis] = 0.0f;
                if (axis == 1 && delta < 0.0f)
                    body.onGround = true;
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
