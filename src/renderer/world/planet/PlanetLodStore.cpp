//
// Created by raph on 29/07/2026.
//

#include "PlanetLodStore.h"

#include "core/log/Logger.h"

using namespace vp;

void PlanetLodStore::init(uint32_t maxNodes) {
    m_maxBlocks = (maxNodes + BLOCK_SIZE - 1) / BLOCK_SIZE;
    m_nodes.assign(static_cast<size_t>(m_maxBlocks) * BLOCK_SIZE, GpuNode{});
    m_inDirty.assign(m_nodes.size(), 0);
    m_dirty.clear();
    m_freeBlocks.clear();
    m_neverDistributedIndex = 0;
}

uint32_t PlanetLodStore::allocate_block() {
    if (!m_freeBlocks.empty()) {
        const uint32_t firstIndex = m_freeBlocks.back();
        m_freeBlocks.pop_back();
        return firstIndex;
    }

    if (m_neverDistributedIndex < m_maxBlocks) {
        return m_neverDistributedIndex++ * BLOCK_SIZE;
    }

    LOG_WARN("PlanetLodStore", "Can't allocate a block, saturated ({} blocks)", m_maxBlocks);
    return NODE_INVALID_PTR;
}

void PlanetLodStore::free_block(uint32_t first) {
    for (uint32_t i = 0; i < BLOCK_SIZE; ++i) {
        recycle(first + i);
    }
    m_freeBlocks.push_back(first);
}

void PlanetLodStore::recycle(uint32_t index) {
    // Wrapping at 256 is fine: it would take 256 reuses of the same slot within the few frames a
    // request or a job result stays in flight for a stale one to be mistaken for a fresh one
    const uint32_t nextGeneration = (m_nodes[index].generation + 1u) & 0xFFu;

    m_nodes[index] = GpuNode{};
    m_nodes[index].generation = nextGeneration;

    mark_dirty(index);
}

void PlanetLodStore::mark_dirty(uint32_t index) {
    if (m_inDirty[index]) return;
    m_inDirty[index] = 1;
    m_dirty.push_back(index);
}

void PlanetLodStore::clear_dirty() {
    for (const uint32_t index: m_dirty) {
        m_inDirty[index] = 0;
    }
    m_dirty.clear();
}