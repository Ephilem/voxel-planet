#include "PlanetLodJobs.h"

using namespace vp;

uint64_t PlanetLodJobs::open(uint32_t nodeIndex, const PlanetNodeCoord &coord) {
    uint32_t slotIndex;

    if (!m_free.empty()) {
        slotIndex = m_free.back();
        m_free.pop_back();
    } else {
        slotIndex = static_cast<uint32_t>(m_slots.size());
        m_slots.emplace_back();
    }

    Slot &slot = m_slots[slotIndex];
    slot.job = {nodeIndex, coord};
    slot.open = true;

    return (static_cast<uint64_t>(slotIndex) << 32) | slot.generation;
}

const PlanetLodJobs::Slot *PlanetLodJobs::resolve(uint64_t token) const {
    const uint32_t slotIndex = token_slot(token);
    if (slotIndex >= m_slots.size()) return nullptr;

    const Slot &slot = m_slots[slotIndex];
    if (!slot.open || slot.generation != token_generation(token)) return nullptr;

    return &slot;
}

bool PlanetLodJobs::peek(uint64_t token, Job &out) const {
    const Slot *slot = resolve(token);
    if (slot == nullptr) return false;

    out = slot->job;
    return true;
}

bool PlanetLodJobs::close(uint64_t token, Job &out) {
    const Slot *found = resolve(token);
    if (found == nullptr) return false;

    const uint32_t slotIndex = token_slot(token);
    Slot &slot = m_slots[slotIndex];

    out = slot.job;
    slot.open = false;
    // Wrapping is fine: it would take four billion jobs through the same slot to collide, and
    // by then the stale result it could be confused with is long gone
    ++slot.generation;

    m_free.push_back(slotIndex);
    return true;
}
