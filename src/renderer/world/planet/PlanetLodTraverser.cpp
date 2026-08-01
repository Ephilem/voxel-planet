//
// Created by raph on 01/08/2026.
//

#include "PlanetLodTraverser.h"

#include "core/log/Logger.h"

using namespace vp;

namespace {
    /// Index of a traversal queue counter, in uint units, as the shaders address them.
    constexpr uint32_t counter_index(int queue) {
        return (queue == 0 ? LOD_COUNTER_OFFSET_QUEUE_0_COUNT : LOD_COUNTER_OFFSET_QUEUE_1_COUNT) / 4u;
    }

    /// Three uint per VkDispatchIndirectCommand.
    constexpr uint32_t DISPATCH_ARGS_UINTS = 3;

    /// NVRHI remaps binding slots per category on Vulkan. Zeroing every offset keeps the slot
    /// numbers below identical to the binding numbers written in the GLSL.
    nvrhi::VulkanBindingOffsets identity_binding_offsets() {
        return nvrhi::VulkanBindingOffsets()
                .setShaderResourceOffset(0)
                .setUnorderedAccessViewOffset(0)
                .setSamplerOffset(0)
                .setConstantBufferOffset(0);
    }
}

void PlanetLodTraverser::init(VulkanBackend *backend, ResourceSystem *resources,
                              PlanetLodGpuBuffers *buffers, uint32_t maxNodes) {
    m_backend = backend;
    m_resourceSystem = resources;
    m_buffers = buffers;
    m_maxNodes = maxNodes;

    init_uniform_buffer();
    init_traversal_pipeline();
    init_prepare_pipeline();
}

void PlanetLodTraverser::init_uniform_buffer() {
    nvrhi::BufferDesc desc;
    desc.byteSize = sizeof(PlanetLodUBO);
    desc.isConstantBuffer = true;
    desc.debugName = "PlanetLod/UBO";
    desc.initialState = nvrhi::ResourceStates::ConstantBuffer;
    desc.keepInitialState = true;

    m_uboBuffer = m_backend->device->createBuffer(desc);
}

void PlanetLodTraverser::init_traversal_pipeline() {
    std::shared_ptr<ShaderResource> shaderRes =
            m_resourceSystem->load<ShaderResource>("planet_lod_traversal.comp", ResourceType::SHADER);

    auto shaderDesc = nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute);
    m_traversalShader = m_backend->device->createShader(shaderDesc, shaderRes->get_data(), shaderRes->get_data_size());

    auto frameLayoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Compute)
            .addItem(nvrhi::BindingLayoutItem::ConstantBuffer(0))
            .setBindingOffsets(identity_binding_offsets());
    m_frameBindingLayout = m_backend->device->createBindingLayout(frameLayoutDesc);

    auto frameSetDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::ConstantBuffer(0, m_uboBuffer));
    m_frameBindingSet = m_backend->device->createBindingSet(frameSetDesc, m_frameBindingLayout);

    // Slot numbers match the binding numbers in planet_lod_common.glsl.
    auto traversalLayoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Compute)
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(0)) // nodes
            .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(1)) // counters
            .addItem(nvrhi::BindingLayoutItem::StructuredBuffer_UAV(2)) // requests
            .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(3)) // render queue
            .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(4)) // in queue
            .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(5)) // out queue
            .addItem(nvrhi::BindingLayoutItem::PushConstants(6, sizeof(TraversalPushConstants)))
            .setBindingOffsets(identity_binding_offsets());
    m_traversalBindingLayout = m_backend->device->createBindingLayout(traversalLayoutDesc);

    // Two sets, differing only by which scratch queue is the input and which is the output.
    for (int inputQueue = 0; inputQueue < 2; ++inputQueue) {
        const int outputQueue = 1 - inputQueue;

        auto setDesc = nvrhi::BindingSetDesc()
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(0, m_buffers->nodes()))
                .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(1, m_buffers->counters()))
                .addItem(nvrhi::BindingSetItem::StructuredBuffer_UAV(2, m_buffers->requests()))
                .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(3, m_buffers->render_queue()))
                .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(4, m_buffers->scratch(inputQueue)))
                .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(5, m_buffers->scratch(outputQueue)))
                .addItem(nvrhi::BindingSetItem::PushConstants(6, sizeof(TraversalPushConstants)));

        m_traversalBindingSet[inputQueue] = m_backend->device->createBindingSet(setDesc, m_traversalBindingLayout);
    }

    auto pipelineDesc = nvrhi::ComputePipelineDesc()
            .setComputeShader(m_traversalShader)
            .addBindingLayout(m_frameBindingLayout)
            .addBindingLayout(m_traversalBindingLayout);
    m_traversalPipeline = m_backend->device->createComputePipeline(pipelineDesc);
}

void PlanetLodTraverser::init_prepare_pipeline() {
    std::shared_ptr<ShaderResource> shaderRes =
            m_resourceSystem->load<ShaderResource>("planet_lod_prepare.comp", ResourceType::SHADER);

    auto shaderDesc = nvrhi::ShaderDesc().setShaderType(nvrhi::ShaderType::Compute);
    m_prepareShader = m_backend->device->createShader(shaderDesc, shaderRes->get_data(), shaderRes->get_data_size());

    auto layoutDesc = nvrhi::BindingLayoutDesc()
            .setVisibility(nvrhi::ShaderType::Compute)
            .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(0)) // counters
            .addItem(nvrhi::BindingLayoutItem::RawBuffer_UAV(1)) // dispatch args
            .addItem(nvrhi::BindingLayoutItem::PushConstants(2, sizeof(PreparePushConstants)))
            .setBindingOffsets(identity_binding_offsets());
    m_prepareBindingLayout = m_backend->device->createBindingLayout(layoutDesc);

    auto setDesc = nvrhi::BindingSetDesc()
            .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(0, m_buffers->counters()))
            .addItem(nvrhi::BindingSetItem::RawBuffer_UAV(1, m_buffers->dispatch_args()))
            .addItem(nvrhi::BindingSetItem::PushConstants(2, sizeof(PreparePushConstants)));
    m_prepareBindingSet = m_backend->device->createBindingSet(setDesc, m_prepareBindingLayout);

    auto pipelineDesc = nvrhi::ComputePipelineDesc()
            .setComputeShader(m_prepareShader)
            .addBindingLayout(m_prepareBindingLayout);
    m_preparePipeline = m_backend->device->createComputePipeline(pipelineDesc);
}

void PlanetLodTraverser::traverse(nvrhi::CommandListHandle cmd, const PlanetLodUBO &frameData, uint32_t rootCount) {
    if (rootCount == 0) return;

    cmd->writeBuffer(m_uboBuffer, &frameData, sizeof(PlanetLodUBO));

    int inputQueue = 0;

    // Root level only: the CPU seeded the queue itself, so it knows how many nodes are in it.
    const uint32_t rootGroups = (rootCount + TRAVERSAL_GROUP_SIZE - 1) / TRAVERSAL_GROUP_SIZE;
    dispatch_traversal(cmd, inputQueue, PLANET_MAX_LOD, rootGroups);
    inputQueue = 1 - inputQueue;

    // Every level below is sized by the GPU: prepare reads the count the level above produced,
    // turns it into dispatch arguments, and clears the counter of the next output queue.
    for (int level = static_cast<int>(PLANET_MAX_LOD) - 1; level >= 0; --level) {
        dispatch_prepare(cmd, inputQueue, static_cast<uint32_t>(level));
        dispatch_traversal(cmd, inputQueue, static_cast<uint32_t>(level), 0);
        inputQueue = 1 - inputQueue;
    }
}

void PlanetLodTraverser::dispatch_traversal(nvrhi::CommandListHandle cmd, int inputQueue, uint32_t level,
                                            uint32_t directGroupCount) {
    const bool indirect = directGroupCount == 0;

    auto state = nvrhi::ComputeState()
            .setPipeline(m_traversalPipeline)
            .addBindingSet(m_frameBindingSet)
            .addBindingSet(m_traversalBindingSet[inputQueue]);

    if (indirect) state.setIndirectParams(m_buffers->dispatch_args());

    // setComputeState() is what makes NVRHI place the UAV barriers between two dispatches that
    // share a buffer, so it has to be called before every single dispatch. Hoisting it out of
    // the loop as an optimisation would silently drop all of them.
    cmd->setComputeState(state);

    TraversalPushConstants push{};
    push.inCountIndex = counter_index(inputQueue);
    push.outCountIndex = counter_index(1 - inputQueue);
    push.maxQueueEntries = m_maxNodes;
    cmd->setPushConstants(&push, sizeof(push));

    if (indirect) {
        cmd->dispatchIndirect(PlanetLodGpuBuffers::dispatch_args_offset(level));
    } else {
        cmd->dispatch(directGroupCount);
    }
}

void PlanetLodTraverser::dispatch_prepare(nvrhi::CommandListHandle cmd, int inputQueue, uint32_t level) {
    auto state = nvrhi::ComputeState()
            .setPipeline(m_preparePipeline)
            .addBindingSet(m_prepareBindingSet);

    cmd->setComputeState(state);

    PreparePushConstants push{};
    push.readCountIndex = counter_index(inputQueue);
    push.zeroCountIndex = counter_index(1 - inputQueue);
    push.argsOffset = level * DISPATCH_ARGS_UINTS;
    push.maxQueueEntries = m_maxNodes;
    push.groupSize = TRAVERSAL_GROUP_SIZE;
    cmd->setPushConstants(&push, sizeof(push));

    cmd->dispatch(1, 1, 1);
}
