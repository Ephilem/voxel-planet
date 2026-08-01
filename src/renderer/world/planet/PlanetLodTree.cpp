//
// Created by raph on 29/07/2026.
//

#include "PlanetLodTree.h"

#include <cstdlib>

#include "core/log/Logger.h"

using namespace vp;

void PlanetLodTree::init(const Config &config) {
    m_config = config;
    m_store.init(config.maxNodes);

    m_roots.clear();
    m_rootIndices.clear();
    m_pending.clear();
    m_childToPending.clear();

    m_inFlight = 0;
    m_hasCenter = false;
}

void PlanetLodTree::ingest_requests(std::span<const GpuLodRequest> requests) {
    for (const GpuLodRequest &request: requests) {
        if (request.nodeIndex >= m_store.capacity()) continue;

        GpuNode &node = m_store.at(request.nodeIndex);

        // Already working on it. The mirror keeps NODE_REQUESTED set for the whole lifetime of
        // the job, so this also absorbs the duplicates the GPU emits when a node upload happens
        // to overwrite the flag it had just set.
        if (node.flags & NODE_REQUESTED) continue;

        // Nothing to generate, but the GPU does not know that yet.
        if (node.flags & NODE_EMPTY) {
            rearm(request.nodeIndex);
            continue;
        }

        // Out of budget for this frame. Rearm so the request comes back instead of being lost.
        if (m_inFlight >= m_config.maxJobInFlight) {
            rearm(request.nodeIndex);
            continue;
        }

        if (request.type == REQ_MESH) {
            handle_mesh_request(request.nodeIndex, request.priority);
        } else if (request.type == REQ_CHILDREN) {
            handle_children_request(request.nodeIndex, request.priority);
        }
    }
}

void PlanetLodTree::handle_mesh_request(uint32_t nodeIndex, uint32_t priority) {
    GpuNode &node = m_store.at(nodeIndex);

    if (node.flags & NODE_HAS_MESH) {
        rearm(nodeIndex);
        return;
    }

    node.flags |= NODE_REQUESTED;
    submit_job(nodeIndex, node_coord(node), priority);
}

void PlanetLodTree::handle_children_request(uint32_t nodeIndex, uint32_t priority) {
    GpuNode &parent = m_store.at(nodeIndex);

    if (node_has_children(parent)) {
        rearm(nodeIndex);
        return;
    }

    const PlanetNodeCoord parentCoord = node_coord(parent);

    // Finest level reached: tell the GPU to stop asking rather than leaving it hanging
    if (parentCoord.level == 0) {
        parent.flags |= NODE_FINEST;
        rearm(nodeIndex);
        return;
    }

    const uint32_t block = m_store.allocate_block();
    if (block == NODE_INVALID_PTR) {
        rearm(nodeIndex); // store saturated, retry on a later frame
        return;
    }

    parent.flags |= NODE_REQUESTED;

    m_pending.push_back({nodeIndex, block, 0, 0});
    const size_t pendingIndex = m_pending.size() - 1;

    for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
        const PlanetNodeCoord childCoord = parentCoord.child(i);

        m_store.at(block + i) = make_node(childCoord, NODE_REQUESTED);
        m_childToPending[block + i] = pendingIndex;

        submit_job(block + i, childCoord, priority);
    }

    // The children are deliberately left out of the dirty list: they must stay invisible to the
    // GPU until publish_subdivision() wires them into the parent
}

void PlanetLodTree::on_job_done(const LodJobResult &result) {
    if (m_inFlight > 0) --m_inFlight;
    if (result.nodeIndex >= m_store.capacity()) return;

    GpuNode &node = m_store.at(result.nodeIndex);

    // The slot may have been recycled while the job was running, in which case the result
    // belongs to a node that no longer exists.
    if (!(node_coord(node) == result.coord)) {
        if (!result.empty && m_releaseMesh) m_releaseMesh(result.meshId);
        return;
    }

    node.flags &= ~NODE_REQUESTED;
    if (result.empty) {
        node.flags |= NODE_EMPTY;
    } else {
        node.flags |= NODE_HAS_MESH;
        node.meshId = result.meshId;
    }

    const auto it = m_childToPending.find(result.nodeIndex);
    if (it == m_childToPending.end()) {
        m_store.mark_dirty(result.nodeIndex);
        return;
    }

    const size_t pendingIndex = it->second;
    PendingSubdivision &pending = m_pending[pendingIndex];

    const uint32_t slot = result.nodeIndex - pending.childBlock;
    pending.satisfiedMask |= static_cast<uint8_t>(1u << slot);
    if (result.empty) pending.emptyMask |= static_cast<uint8_t>(1u << slot);

    if (pending.satisfiedMask == 0xFF) publish_subdivision(pendingIndex);
}

void PlanetLodTree::publish_subdivision(size_t pendingIndex) {
    const PendingSubdivision pending = m_pending[pendingIndex];

    for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
        m_childToPending.erase(pending.childBlock + i);
    }

    GpuNode &parent = m_store.at(pending.parentIndex);

    if (pending.emptyMask == 0xFF) {
        // Every child came back uniform, so subdividing gained nothing. Drop the block and mark
        // the parent itself uniform, which also stops the GPU from asking again.
        m_store.free_block(pending.childBlock);
        parent.flags |= NODE_EMPTY;
    } else {
        for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
            m_store.mark_dirty(pending.childBlock + i);
        }
        parent.childPtr = pending.childBlock;
        parent.childMask = static_cast<uint8_t>(~pending.emptyMask);
    }

    parent.flags &= ~NODE_REQUESTED;
    m_store.mark_dirty(pending.parentIndex);

    // Swap and pop, then fix up the indices of the entry that moved into this slot.
    m_pending[pendingIndex] = m_pending.back();
    m_pending.pop_back();

    if (pendingIndex < m_pending.size()) {
        const PendingSubdivision &moved = m_pending[pendingIndex];
        for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
            const auto it = m_childToPending.find(moved.childBlock + i);
            if (it != m_childToPending.end()) it->second = pendingIndex;
        }
    }
}

void PlanetLodTree::update_roots(CubeFace face, int32_t rootU, int32_t rootV) {
    if (m_hasCenter && face == m_centerFace && rootU == m_centerU && rootV == m_centerV) return;

    m_centerFace = face;
    m_centerU = rootU;
    m_centerV = rootV;
    m_hasCenter = true;

    const int radius = m_config.rootRadius;
    const int radiusSq = radius * radius;

    for (auto it = m_roots.begin(); it != m_roots.end();) {
        const int du = it->first.u - rootU;
        const int dv = it->first.v - rootV;

        if (it->first.face != face || du * du + dv * dv > radiusSq) {
            destroy_subtree(it->second);
            m_store.free_block(it->second);
            it = m_roots.erase(it);
        } else {
            ++it;
        }
    }

    // u and v are stored on 16 signed bits at each node's own level, so a root has to stay
    // within the range its level 0 descendants can still address. At root level 8 that leaves
    // 127 roots in each direction, a bit over 1000 km of flat terrain
    const int32_t rootLimit = NODE_UV_MAX >> m_config.rootLevel;

    // A full rescan of the disc, a few hundred iterations at most. An incremental ring delta
    // would only pay off with a much larger root count.
    for (int dv = -radius; dv <= radius; ++dv) {
        for (int du = -radius; du <= radius; ++du) {
            if (du * du + dv * dv > radiusSq) continue;

            for (int alt = m_config.rootAltMin; alt <= m_config.rootAltMax; ++alt) {
                const PlanetNodeCoord coord{face, m_config.rootLevel, rootU + du, rootV + dv, alt};
                if (std::abs(coord.u) > rootLimit || std::abs(coord.v) > rootLimit) continue;
                if (m_roots.contains(coord)) continue;

                const uint32_t index = create_root(coord);
                if (index != NODE_INVALID_PTR) m_roots.emplace(coord, index);
            }
        }
    }

    m_rootIndices.clear();
    m_rootIndices.reserve(m_roots.size());
    for (const auto &[coord, index]: m_roots) {
        m_rootIndices.push_back(index);
    }
}

uint32_t PlanetLodTree::create_root(const PlanetNodeCoord &coord) {
    const uint32_t block = m_store.allocate_block();
    if (block == NODE_INVALID_PTR) return NODE_INVALID_PTR;

    m_store.at(block) = make_node(coord);
    m_store.mark_dirty(block);
    return block;
}

void PlanetLodTree::destroy_subtree(uint32_t nodeIndex) {
    GpuNode &node = m_store.at(nodeIndex);

    if (node_has_children(node)) {
        const uint32_t block = node.childPtr;
        for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
            destroy_subtree(block + i);
        }
        m_store.free_block(block);
        node.childPtr = NODE_INVALID_PTR;
        node.childMask = 0;
    }

    if ((node.flags & NODE_HAS_MESH) && m_releaseMesh) {
        m_releaseMesh(node.meshId);
    }

    // A job may still be running for this node. It will find the slot recycled and drop itself
    node = GpuNode{};
    m_store.mark_dirty(nodeIndex);
}

void PlanetLodTree::rearm(uint32_t nodeIndex) {
    // The mirror never carries NODE_REQUESTED here, so uploading the node as it stands is what
    // clears the bit the traversal shader set and lets it emit the request again
    m_store.mark_dirty(nodeIndex);
}

void PlanetLodTree::submit_job(uint32_t nodeIndex, const PlanetNodeCoord &coord, uint32_t priority) {
    if (!m_submitJob) {
        LOG_WARN("PlanetLodTree", "No job submission callback installed, request dropped");
        return;
    }

    ++m_inFlight;
    m_submitJob(nodeIndex, coord, priority);
}