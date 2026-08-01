//
// Created by raph on 29/07/2026.
//

#include "PlanetLodTree.h"

#include <algorithm>
#include <cstdlib>

#include "core/log/Logger.h"
#include "core/world/world_components.h"

using namespace vp;

void PlanetLodTree::init(const Config &config) {
    m_config = config;
    m_store.init(config.maxNodes);

    m_roots.clear();
    m_rootIndices.clear();
    m_freeRootSlots.clear();
    m_pending.clear();
    m_childToPending.clear();

    m_inFlight = 0;
    m_strandedDropped = 0;
    m_hasCenter = false;
}

void PlanetLodTree::ingest_requests(std::span<const GpuLodRequest> requests) {
    ++m_frame;

    for (const GpuLodRequest &request: requests) {
        if (request.nodeIndex >= m_store.capacity()) continue;

        GpuNode &node = m_store.at(request.nodeIndex);

        // The request was emitted MAX_FRAMES_IN_FLIGHT frames ago. Since then the slot may have
        // been released, or released and handed to a different node. Neither is detectable from
        // the contents: a released slot is all zeroes, which reads as a valid level 0 node at the
        // origin, and the same coordinate is routinely recreated as the player moves around.
        //
        // Acting on a dead slot is not harmless. The tree would mesh it, and that geometry is
        // reachable from nothing, so it is never released: the arena fills up, allocations start
        // failing, and failed allocations are reported as uniform nodes, which is a permanent
        // hole. Both tests are needed, the flag for a slot that was never handed out at all.
        if (!(node.flags & NODE_ALIVE)) continue;
        if (node.generation != request.generation) continue;

        // Nothing to generate, but the GPU does not know that yet.
        if (node.flags & NODE_EMPTY) {
            rearm(request.nodeIndex);
            continue;
        }

        // Merges cost nothing and free memory, so they are handled before the budget check.
        // They are also the only thing that reclaims a subtree the shader can still see: the
        // decision is made by the traversal, never guessed at here.
        if (request.type == REQ_MERGE) {
            // The shader only asks for this on a node it believes owns geometry, but the mirror
            // is a frame or two ahead of what the shader saw. Dropping the children of a node
            // with nothing of its own to draw would open a hole with no way to fill it.
            if ((node.flags & NODE_HAS_MESH) && node_has_children(node)) {
                m_collapsedByGpu += collapse_children(request.nodeIndex);
            }
            rearm(request.nodeIndex);
            continue;
        }

        // Already working on this exact kind of request. The two have their own flag so a node
        // that owns neither geometry nor children can have both in flight at once, which is what
        // lets it be drawn coarsely while its subtree is being built.
        const uint8_t pendingBit = request.type == REQ_MESH ? NODE_REQ_MESH : NODE_REQ_SPLIT;
        if (node.flags & pendingBit) continue;

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

    // Either it already has geometry, or the CPU has already established it has none. The shader
    // checks both too, but a request in flight predates whatever the mirror knows now
    if (node.flags & (NODE_HAS_MESH | NODE_NO_MESH)) {
        rearm(nodeIndex);
        return;
    }

    node.flags |= NODE_REQ_MESH;
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

    parent.flags |= NODE_REQ_SPLIT;

    m_pending.push_back({nodeIndex, block, 0, 0, m_frame});
    const size_t pendingIndex = m_pending.size() - 1;

    for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
        const PlanetNodeCoord childCoord = parentCoord.child(i);

        place_node(block + i, childCoord, NODE_REQ_MESH);
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

    // The slot may have been recycled while the job was running, in which case the result belongs
    // to a node that no longer exists. Comparing coordinates is not enough: destroying a node and
    // recreating the same coordinate a few frames later is the normal thing that happens when the
    // player moves back and forth, and the stale result would then be accepted for a node that
    // never asked for it.
    if (!(node.flags & NODE_ALIVE) || node.generation != result.generation) {
        if (!result.empty && m_releaseMesh) m_releaseMesh(result.meshId);
        return;
    }

    node.flags &= ~NODE_REQ_MESH;
    if (result.empty) {
        node.flags |= NODE_NO_MESH;

        // Emptiness is hereditary in this generator: it only reports a node uniform when the node
        // sits entirely above the highest possible ground or entirely below the solid shell, and
        // a child is contained in its parent. So a node with nothing of its own and no
        // subdivision under way has nothing anywhere below it either, and the traversal can stop
        // visiting it altogether.
        //
        // The guard matters: with a subdivision in flight the children may well have geometry,
        // and marking the parent uniform would make the traversal skip the whole subtree the
        // moment it published.
        if (!(node.flags & NODE_REQ_SPLIT) && !node_has_children(node)) {
            node.flags |= NODE_EMPTY;
        }
    } else {
        // A node can already own geometry here: destroying and rebuilding the same coordinate
        // leaves the first job running, and both results then match. Overwriting the id without
        // handing the old one back loses its face regions for the rest of the session
        if ((node.flags & NODE_HAS_MESH) && node.meshId != result.meshId && m_releaseMesh) {
            m_releaseMesh(node.meshId);
        }

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

    parent.flags &= ~NODE_REQ_SPLIT;
    m_store.mark_dirty(pending.parentIndex);

    erase_pending(pendingIndex);
}

void PlanetLodTree::erase_pending(size_t pendingIndex) {
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

void PlanetLodTree::drop_pending_subdivision(uint32_t parentIndex) {
    for (size_t i = 0; i < m_pending.size(); ++i) {
        if (m_pending[i].parentIndex != parentIndex) continue;

        const uint32_t block = m_pending[i].childBlock;

        for (uint32_t c = 0; c < PlanetLodStore::BLOCK_SIZE; ++c) {
            m_childToPending.erase(block + c);

            // Some children may already have come back with geometry, even though the
            // subdivision as a whole never got published
            const GpuNode &child = m_store.at(block + c);
            if ((child.flags & NODE_HAS_MESH) && m_releaseMesh) m_releaseMesh(child.meshId);
        }

        // Bumps every child's generation, which is what makes the jobs still running for these
        // slots recognise themselves as stale and drop their results
        m_store.free_block(block);

        // The parent is left free to ask again. Without this it keeps NODE_REQ_SPLIT for good and
        // the traversal never emits another request for it
        GpuNode &parent = m_store.at(parentIndex);
        parent.flags &= ~NODE_REQ_SPLIT;
        m_store.mark_dirty(parentIndex);

        erase_pending(i);
        return;
    }
}

uint32_t PlanetLodTree::sweep_stranded(uint64_t maxAge) {
    uint32_t dropped = 0;

    // Backwards, because drop_pending_subdivision() erases by swapping the last entry into the
    // hole: everything past the current index has already been looked at
    for (size_t i = m_pending.size(); i-- > 0;) {
        if (m_frame - m_pending[i].openedFrame < maxAge) continue;

        const uint32_t parentIndex = m_pending[i].parentIndex;
        LOG_WARN("PlanetLodTree",
                 "Subdivision of node {} stranded for {} frames, dropping it",
                 parentIndex, m_frame - m_pending[i].openedFrame);

        drop_pending_subdivision(parentIndex);
        ++dropped;
    }

    m_strandedDropped += dropped;
    return dropped;
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
            // destroy_subtree() already recycles the slot itself, all that is left is to let
            // another root have it
            destroy_subtree(it->second);
            free_root_slot(it->second);
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

uint32_t PlanetLodTree::alloc_root_slot() {
    if (m_freeRootSlots.empty()) {
        const uint32_t block = m_store.allocate_block();
        if (block == NODE_INVALID_PTR) return NODE_INVALID_PTR;

        for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
            m_freeRootSlots.push_back(block + i);
        }
    }

    const uint32_t slot = m_freeRootSlots.back();
    m_freeRootSlots.pop_back();
    return slot;
}

uint32_t PlanetLodTree::create_root(const PlanetNodeCoord &coord) {
    const uint32_t index = alloc_root_slot();
    if (index == NODE_INVALID_PTR) return NODE_INVALID_PTR;

    place_node(index, coord, 0);
    m_store.mark_dirty(index);
    return index;
}

void PlanetLodTree::place_node(uint32_t index, const PlanetNodeCoord &coord, uint8_t flags) {
    const auto generation = static_cast<uint8_t>(m_store.at(index).generation);
    m_store.at(index) = make_node(coord, flags, generation);
}

void PlanetLodTree::destroy_subtree(uint32_t nodeIndex) {
    GpuNode &node = m_store.at(nodeIndex);

    // A subdivision still in flight is not linked through childPtr, so the recursion below would
    // walk straight past it and leak the whole block
    if (node.flags & NODE_REQ_SPLIT) drop_pending_subdivision(nodeIndex);

    // This node may itself be a child of a subdivision that has not been published. Leaving the
    // mapping behind would credit a later job on the recycled slot to that subdivision.
    m_childToPending.erase(nodeIndex);

    if (node_has_children(node)) {
        const uint32_t block = node.childPtr;

        // A node can never be its own descendant. Recursing into the block this node lives in
        // would never terminate, and a blown stack says nothing about what went wrong, so the
        // subtree is abandoned and reported instead.
        const uint32_t ownBlock = nodeIndex - (nodeIndex % PlanetLodStore::BLOCK_SIZE);
        if (block == ownBlock || block + PlanetLodStore::BLOCK_SIZE > m_store.capacity()) {
            LOG_ERROR("PlanetLodTree",
                      "Node {} points at an impossible child block {}, leaking it to stay alive",
                      nodeIndex, block);
        } else {
            for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
                destroy_subtree(block + i);
            }
            m_store.free_block(block);
        }

        node.childPtr = NODE_INVALID_PTR;
        node.childMask = 0;
    }

    if ((node.flags & NODE_HAS_MESH) && m_releaseMesh) {
        m_releaseMesh(node.meshId);
    }

    // A job may still be running for this node. Bumping the generation is what makes it find the
    // slot recycled and drop itself
    m_store.recycle(nodeIndex);
}

namespace {
    /// Distance from a point to an axis aligned box, zero when the point is inside
    float distance_to_aabb(const glm::vec3 &point, const glm::vec3 &boundsMin, const glm::vec3 &boundsMax) {
        const glm::vec3 outside = glm::max(glm::max(boundsMin - point, point - boundsMax), glm::vec3(0.0f));
        return glm::length(outside);
    }
}

uint32_t PlanetLodTree::collapse_distant(const glm::vec3 &cameraWorldPos, float keepFactor) {
    uint32_t released = 0;

    // The root indices are stable across the walk: collapsing only ever frees blocks below a
    // root, never a root itself. Roots are the business of update_roots()
    for (const uint32_t rootIndex: m_rootIndices) {
        released += collapse_node(rootIndex, cameraWorldPos, keepFactor);
    }

    return released;
}

uint32_t PlanetLodTree::collapse_node(uint32_t nodeIndex, const glm::vec3 &cameraWorldPos, float keepFactor) {
    const GpuNode &node = m_store.at(nodeIndex);
    if (!node_has_children(node)) return 0;

    const PlanetNodeCoord coord = node_coord(node);

    // Mirror of lod_node_corner() in planet_lod_common.glsl: node coordinates map straight onto
    // world axes, u to x, alt to y, v to z
    const float nodeSize = static_cast<float>(CHUNK_SIZE) * static_cast<float>(1u << coord.level);
    const glm::vec3 boundsMin =
            glm::vec3(static_cast<float>(coord.u), static_cast<float>(coord.alt), static_cast<float>(coord.v))
            * nodeSize;

    const float distance = distance_to_aabb(cameraWorldPos, boundsMin, boundsMin + glm::vec3(nodeSize));

    // Collapsing a node that owns no geometry would leave nothing to draw in its place until the
    // GPU notices and asks for a mesh, so those keep their children. In practice that only ever
    // applies to roots, which are created empty
    if (distance > nodeSize * keepFactor && (node.flags & NODE_HAS_MESH)) {
        return collapse_children(nodeIndex);
    }

    uint32_t released = 0;
    const uint32_t block = node.childPtr;
    const uint32_t mask = node.childMask;

    for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
        if ((mask & (1u << i)) == 0u) continue;
        released += collapse_node(block + i, cameraWorldPos, keepFactor);
    }

    return released;
}

uint32_t PlanetLodTree::collapse_children(uint32_t nodeIndex) {
    GpuNode &node = m_store.at(nodeIndex);
    const uint32_t block = node.childPtr;

    // Unlink first. The GPU stops descending into the block the moment this node is uploaded,
    // and it falls back on the mesh this node already owns
    node.childPtr = NODE_INVALID_PTR;
    node.childMask = 0;
    m_store.mark_dirty(nodeIndex);

    uint32_t released = 0;
    for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
        released += 1 + count_subtree(block + i);
        destroy_subtree(block + i);
    }
    m_store.free_block(block);

    return released;
}

uint32_t PlanetLodTree::count_subtree(uint32_t nodeIndex) const {
    const GpuNode &node = m_store.at(nodeIndex);
    if (!node_has_children(node)) return 0;

    uint32_t total = 0;
    for (uint32_t i = 0; i < PlanetLodStore::BLOCK_SIZE; ++i) {
        if ((node.childMask & (1u << i)) == 0u) continue;
        total += 1 + count_subtree(node.childPtr + i);
    }
    return total;
}

uint64_t PlanetLodTree::oldest_pending_age() const {
    uint64_t oldest = m_frame;
    for (const PendingSubdivision &pending: m_pending) {
        oldest = std::min(oldest, pending.openedFrame);
    }
    return m_pending.empty() ? 0 : m_frame - oldest;
}

void PlanetLodTree::rearm(uint32_t nodeIndex) {
    // Uploading the node as it stands is what clears the request bits the traversal shader set:
    // the mirror only carries them for the kinds of job that really are running, so a rearm
    // cancels the shader's claim without cancelling a genuine one
    m_store.mark_dirty(nodeIndex);
}

void PlanetLodTree::submit_job(uint32_t nodeIndex, const PlanetNodeCoord &coord, uint32_t priority) {
    if (!m_submitJob) {
        LOG_WARN("PlanetLodTree", "No job submission callback installed, request dropped");
        return;
    }

    ++m_inFlight;
    m_submitJob(nodeIndex, coord, static_cast<uint8_t>(m_store.at(nodeIndex).generation), priority);
}