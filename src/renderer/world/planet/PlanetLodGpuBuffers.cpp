//
// Created by raph on 29/07/2026.
//

#include "PlanetLodGpuBuffers.h"

#include <algorithm>

#include "core/TracyIntegration.h"
#include "core/log/Logger.h"

using namespace vp;

constexpr uint8_t COUNTER_ZEROS[LOD_COUNTER_BUFFER_SIZE] = {};

/// Total size of the request queue
constexpr uint64_t REQUESTS_BYTES = static_cast<uint64_t>(PlanetLodGpuBuffers::MAX_REQUESTS) * sizeof(GpuLodRequest);

/// A readback staging buffer holds the counters followed by the whole request queue
constexpr uint64_t READBACK_BYTES = PlanetLodGpuBuffers::READBACK_REQUESTS_OFFSET + REQUESTS_BYTES;

PlanetLodGpuBuffers::PlanetLodGpuBuffers(VulkanBackend* backend, uint32_t maxNodes) {
    m_backend = backend;
    m_maxNodes = maxNodes;
    init_gpu();
}

void PlanetLodGpuBuffers::init_gpu() {
    // Everything the compute shaders touch is left in UnorderedAccess with automatic state
    // tracking, so NVRHI inserts the transitions around the writeBuffer and copyBuffer calls.
    auto makeStorage = [&](uint64_t byteSize, uint32_t stride, const char* name) {
        nvrhi::BufferDesc desc;
        desc.byteSize = byteSize;
        desc.structStride = stride; // 0 means a raw buffer
        desc.canHaveUAVs = true;
        desc.canHaveRawViews = stride == 0;
        desc.debugName = name;
        desc.initialState = nvrhi::ResourceStates::UnorderedAccess;
        desc.keepInitialState = true;
        return m_backend->device->createBuffer(desc);
    };

    m_nodes = makeStorage(static_cast<uint64_t>(m_maxNodes) * sizeof(GpuNode), sizeof(GpuNode), "PlanetLod/Nodes");
    m_counters = makeStorage(LOD_COUNTER_BUFFER_SIZE, 0, "PlanetLod/Counters");
    m_requestsQueue = makeStorage(REQUESTS_BYTES, sizeof(GpuLodRequest), "PlanetLod/Requests");

    const uint64_t queueBytes = static_cast<uint64_t>(m_maxNodes) * sizeof(uint32_t);
    m_scratchBuffer[0] = makeStorage(queueBytes, 0, "PlanetLod/QueueA");
    m_scratchBuffer[1] = makeStorage(queueBytes, 0, "PlanetLod/QueueB");

    m_renderQueue = makeStorage(static_cast<uint64_t>(MAX_RENDER) * sizeof(uint32_t), 0, "PlanetLod/RenderQueue");

    // The dispatch args need their own buffer
    nvrhi::BufferDesc argsDesc;
    argsDesc.byteSize = static_cast<uint64_t>(DISPATCH_ARGS_STRIDE) * (PLANET_MAX_LOD + 1);
    argsDesc.canHaveUAVs = true;
    argsDesc.canHaveRawViews = true;
    argsDesc.isDrawIndirectArgs = true;
    argsDesc.debugName = "PlanetLod/DispatchArgs";
    argsDesc.initialState = nvrhi::ResourceStates::IndirectArgument;
    argsDesc.keepInitialState = true;
    m_dispatchArgs = m_backend->device->createBuffer(argsDesc);

    for (uint32_t i = 0; i < REQUESTS_READBACK_COUNT; ++i) {
        nvrhi::BufferDesc readbackDesc;
        readbackDesc.byteSize = READBACK_BYTES;
        readbackDesc.cpuAccess = nvrhi::CpuAccessMode::Read;
        readbackDesc.debugName = "PlanetLod/RequestReadback";
        m_readback[i] = m_backend->device->createBuffer(readbackDesc);
    }

    m_scratchCpu.reserve(MAX_REQUESTS);
}

void PlanetLodGpuBuffers::upload_dirty(nvrhi::CommandListHandle cmd, const PlanetLodStore& store) {
    VOXEL_ZONE_N("PlanetLodGpuBuffers::upload_dirty");
    const std::vector<uint32_t>& dirty = store.dirty();
    if (dirty.empty()) return;

    m_sortedDirty.assign(dirty.begin(), dirty.end());
    std::sort(m_sortedDirty.begin(), m_sortedDirty.end());

    size_t runStart = 0;
    while (runStart < m_sortedDirty.size()) {
        size_t runEnd = runStart + 1;
        while (runEnd < m_sortedDirty.size() && m_sortedDirty[runEnd] == m_sortedDirty[runEnd - 1] + 1) {
            ++runEnd;
        }

        const uint32_t first = m_sortedDirty[runStart];
        const size_t count = runEnd - runStart;

        cmd->writeBuffer(m_nodes,
                         store.data() + first,
                         sizeof(GpuNode) * count,
                         sizeof(GpuNode) * static_cast<uint64_t>(first));

        runStart = runEnd;
    }
}

void PlanetLodGpuBuffers::reset_counters(nvrhi::CommandListHandle cmd) {
    cmd->writeBuffer(m_counters, COUNTER_ZEROS, sizeof(COUNTER_ZEROS));
}

void PlanetLodGpuBuffers::seed_roots(nvrhi::CommandListHandle cmd, const std::vector<uint32_t>& rootIndices) {
    const uint32_t count = static_cast<uint32_t>(rootIndices.size());

    cmd->writeBuffer(m_counters, &count, sizeof(uint32_t), LOD_COUNTER_OFFSET_QUEUE_0_COUNT);

    if (count > 0) {
        cmd->writeBuffer(m_scratchBuffer[0], rootIndices.data(), count * sizeof(uint32_t));
    }
}

void PlanetLodGpuBuffers::snapshot_requests(nvrhi::CommandListHandle cmd, uint64_t frame) {
    const uint32_t slot = static_cast<uint32_t>(frame % REQUESTS_READBACK_COUNT);

    // Two copies into the same staging buffer. Both are recorded after the traversal and before
    // any further write, so the count and the requests it describes stay consistent.
    cmd->copyBuffer(m_readback[slot], 0, m_counters, 0, LOD_COUNTER_BUFFER_SIZE);
    cmd->copyBuffer(m_readback[slot], READBACK_REQUESTS_OFFSET, m_requestsQueue, 0, REQUESTS_BYTES);
}

std::span<const GpuLodRequest> PlanetLodGpuBuffers::read_requests(uint64_t frame) {
    VOXEL_ZONE_N("PlanetLodGpuBuffers::read_requests");
    m_scratchCpu.clear();
    m_lastStats = {};

    // Nothing has been through a full frames cycle
    if (frame < MAX_FRAMES_IN_FLIGHT) return {};

    const uint32_t slot = static_cast<uint32_t>((frame - MAX_FRAMES_IN_FLIGHT) % REQUESTS_READBACK_COUNT);

    void* mapped = m_backend->device->mapBuffer(m_readback[slot], nvrhi::CpuAccessMode::Read);
    if (mapped == nullptr) {
        LOG_WARN("PlanetLodGpuBuffers", "Failed to map the request readback buffer");
        return {};
    }

    const auto* base = static_cast<const uint8_t*>(mapped);
    const uint32_t count = *reinterpret_cast<const uint32_t*>(base + LOD_COUNTER_OFFSET_REQUEST_COUNT);
    const uint32_t overflow = *reinterpret_cast<const uint32_t*>(base + LOD_COUNTER_OFFSET_REQUEST_OVERFLOW);

    m_lastStats.requests = count;
    m_lastStats.requestOverflow = overflow;
    m_lastStats.renderedNodes = *reinterpret_cast<const uint32_t*>(base + LOD_COUNTER_OFFSET_RENDER_COUNT);

    const auto* items = reinterpret_cast<const GpuLodRequest*>(base + READBACK_REQUESTS_OFFSET);
    const uint32_t clamped = std::min(count, MAX_REQUESTS);

    // Copy out before unmapping: the staging buffer is reused by a later frame
    m_scratchCpu.assign(items, items + clamped);
    m_backend->device->unmapBuffer(m_readback[slot]);

    if (overflow > 0) {
        LOG_WARN("PlanetLodGpuBuffers", "Request queue full, {} requests dropped this frame", overflow);
    }

    // Most urgent first, so clipping the list to the in flight budget keeps the nodes whose
    // absence is the most visible
    std::sort(m_scratchCpu.begin(), m_scratchCpu.end(),
              [](const GpuLodRequest& a, const GpuLodRequest& b) { return a.priority > b.priority; });

    return m_scratchCpu;
}