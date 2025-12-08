#include "RendererModule.h"

#include "Renderer.h"

#include "core/GameState.h"
#include "platform/PlatformState.h"
#include "platform/events.h"

#include "vulkan/VulkanBackend.h"

#include <iostream>

#include "Camera3dSystems.h"
#include "rendering_components.h"
#include "core/main_components.h"
#include "core/world/world_components.h"
#include "debug/ImGuiManager.h"
#include "debug/LogConsole.h"
#include "nvrhi/utils.h"
#include "platform/inputs/input_state.h"


RendererModule::RendererModule(flecs::world& ecs) {
    auto* platform = ecs.get<PlatformState>();
    if (!platform || !platform->window) {
        throw std::runtime_error("RendererModule: PlatformModule must be initialized before RendererModule");
    }

    ecs.set<Renderer>({
        .backend = std::make_unique<VulkanBackend>(platform->window->window, RenderParameters{platform->window->width, platform->window->height}),
        .imguiManager = std::make_unique<ImGuiManager>(),
    });

    ecs.system<Renderer>("BeginFrameSystem")
        .kind(flecs::PreStore)
        .each([](flecs::entity e, Renderer& renderer) {
            FrameContext& ctx = renderer.frameContext;
            ctx.frameActive = false;

            if (!renderer.backend) return;

            if (renderer.backend->begin_frame(ctx.commandList)) {
                ctx.commandList->open();

                nvrhi::utils::ClearColorAttachment(ctx.commandList, renderer.backend->get_current_framebuffer(), 0, nvrhi::Color(0.1f, 0.1f, 0.4f, 1.0f));
                nvrhi::utils::ClearDepthStencilAttachment(ctx.commandList, renderer.backend->get_current_framebuffer(), 1.0f, 0);

                ctx.frameActive = true;
            }
        });

    // Initialize chunk meshes for any VoxelChunk that doesn't have a mesh yet
    ecs.system<const VoxelChunk>("InitializeChunkMeshSystem")
        .kind(flecs::OnUpdate)
        .without<VoxelChunkMesh>()
        .each([](flecs::entity e, const VoxelChunk& chunk) {
            e.set<VoxelChunkMesh>({})
             .add<Dirty>();
        });

    VoxelTerrainRenderer::Register(ecs);
    ImGuiManager::Register(ecs);
    ImGuiDebugModuleManager::Register(ecs);

    Camera3dSystems::Register(ecs);

    ecs.system<Orientation>("MouseLookSystem")
        .kind(flecs::OnUpdate)
        .with<Camera3d>()
        .each([](flecs::entity e, Orientation& orientation) {
            auto* inputState = e.world().get_mut<InputState>();
            if (!inputState->mouseCaptured) return;

            float sensitivity = -0.1f;
            orientation.yaw += inputState->mouseDeltaX * sensitivity;
            orientation.yaw = fmod(orientation.yaw, 360.0f);
            orientation.pitch += inputState->mouseDeltaY * sensitivity;
            orientation.pitch = fmod(orientation.pitch, 360.0f);

            if (orientation.pitch > 89.0f) orientation.pitch = 89.0f;
            if (orientation.pitch < -89.0f) orientation.pitch = -89.0f;
        });

    ecs.system<Position, const Orientation>("BasicCameraMovementSystem")
        .kind(flecs::OnUpdate)
        .with<Camera3d>()
        .each([](flecs::entity e, Position& pos, const Orientation& orientation) {
            float moveSpeed = 10.0f;
            const auto* inputState = e.world().get<InputActionState>();

            glm::vec3 forward = glm::vec3(
                cos(glm::radians(orientation.pitch)) * sin(glm::radians(orientation.yaw)),
                0,
                cos(glm::radians(orientation.pitch)) * cos(glm::radians(orientation.yaw))
            );

            // Right vector is always horizontal
            glm::vec3 right = glm::vec3(
                cos(glm::radians(orientation.yaw)),
                0.0f,
                -sin(glm::radians(orientation.yaw))
            );

            glm::vec3 velocity = glm::vec3(0.0f);

            if (inputState->is_action_active(ActionInputType::Forward))
                velocity += forward;
            if (inputState->is_action_active(ActionInputType::Backward))
                velocity -= forward;
            if (inputState->is_action_active(ActionInputType::Left))
                velocity += right;
            if (inputState->is_action_active(ActionInputType::Right))
                velocity -= right;
            if (inputState->is_action_active(ActionInputType::Up))
                velocity += glm::vec3(0.0f, 1.0f, 0.0f);
            if (inputState->is_action_active(ActionInputType::Down))
                velocity -= glm::vec3(0.0f, 1.0f, 0.0f);

            if (glm::length(velocity) > 0.0f) {
                velocity = glm::normalize(velocity) * moveSpeed * static_cast<float>(e.world().get<GameState>()->deltaTime);
                pos += velocity;
            }
        });


    // create camera here for now
    ecs.entity()
        .set<Position>({0.0f, 0.0f, 5.0f})
        .set<Camera3dParameters>({
            .fov = 70.0f
        })
        .set<Orientation>({0.0f, 0.0f, 0.0f})
        .set<Camera3d>({});

    ecs.system<Renderer>("EndFrameSystem")
        .kind(flecs::PostFrame)
        .each([](flecs::entity e, Renderer& renderer) {
            FrameContext& ctx = renderer.frameContext;
            if (!ctx.frameActive || !ctx.commandList) return;

            ctx.commandList->close();
            nvrhi::CommandListHandle cmdList = ctx.commandList;
            renderer.backend->device->executeCommandLists(&cmdList, 1);

            renderer.backend->present();

            ctx.frameActive = false;
        });

    ecs.observer<PlatformState>()
        .event<WindowResizeEvent>()
        .run([](flecs::iter& it) {
            auto* evt = it.param<WindowResizeEvent>();
            auto* renderer = it.world().get_mut<Renderer>();

            if (renderer && renderer->backend) {
                renderer->backend->handle_resize(evt->width, evt->height);
            }
        });
}

void shutdown_renderer(flecs::world& ecs) {
    LOG_INFO("RendererModule", "Shutting down...");
    auto* renderer = ecs.get_mut<Renderer>();
    if (renderer) {
        if (renderer->voxelTerrainRenderer) {
            renderer->voxelTerrainRenderer.reset();
        }
        if (renderer->debugModuleManager) {
            renderer->debugModuleManager.reset();
        }
        if (renderer->imguiManager) {
            renderer->imguiManager.reset();
        }
        renderer->frameContext.commandList = nullptr;
        if (renderer->backend) {
            renderer->backend.reset();
        }
    }
}
