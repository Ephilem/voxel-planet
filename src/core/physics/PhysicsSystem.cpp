//
// Created by raph on 29/03/2026.
//

#include "PhysicsSystem.h"

#include "physics_components.h"
#include "core/main_components.h"

void PhysicsSystem::Register(flecs::world &ecs) {
    PhysicsSystem system;
    system.init(ecs);
}

void PhysicsSystem::init(flecs::world &ecs) {
    ecs.component<Velocity>()
            .member<float>("x", 0, 0)
            .member<float>("y", 0, 0)
            .member<float>("z", 0, 0);

    ecs.system<const Movement, Position>("Physics-ApplyMovementSystem")
            .kind(flecs::OnUpdate)
            .each([](flecs::entity e, const Movement &movement, Position &position) {
                if (movement.direction == glm::vec3(0.0f)) return;
                float dt = e.world().delta_time();

                position.x += movement.direction.x * movement.speed * dt;
                position.y += movement.direction.y * movement.speed * dt;
                position.z += movement.direction.z * movement.speed * dt;
            });

    ecs.system<const Velocity, Position>("Physics-ApplyVelocitySystem")
            .kind(flecs::OnUpdate)
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
