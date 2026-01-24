#include "ChunkManager.h"

#include <algorithm>
#include <vector>

#include "WorldGenerator.h"
#include "platform/inputs/input_state.h"

struct InputActionState;

void ChunkManager::init(flecs::world &ecs) {
    // register systems
    ecs.system<ChunkLoader, const Position>("ChunkManager-UpdateLoadQueueSystem")
        .kind(flecs::OnUpdate)
        .each([this](flecs::entity e, ChunkLoader& loader, const Position& position) {
            const auto* inputState = e.world().get<InputActionState>();
            if (inputState->is_action_pressed(ActionInputType::Debug1)) {
                this->load_chunks_at_radius({0,0,0}, 8);
            }
        });

    ecs.system("ChunkManager-ProcessLoadQueue")
        .kind(flecs::OnStore)
        .run([this](flecs::iter& it) {
            this->process_load_queue_system(it);
        });

    ecs.system("ChunkManager-ProcessUnloadQueue")
        .kind(flecs::OnStore)
        .run([this](flecs::iter& it) {
            this->process_unload_queue_system(it);
        });

}

void ChunkManager::load_chunks_at_radius(const ChunkCoordinate &center, int radius) {
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            for (int z = -radius; z <= radius; z++) {
                glm::ivec3 chunkPos = glm::ivec3(center.x + x, center.y + y, center.z + z);
                if (!is_chunk_processed(chunkPos)) {
                    if (!m_loadingChunks.contains(chunkPos)) {
                        m_loadQueue.push_back(chunkPos);
                        m_loadingChunks.insert(chunkPos);
                    }
                }
            }
        }
    }
}

void ChunkManager::update_desired_chunk_system(flecs::entity e, ChunkLoader &loader, const Position &position) {
    glm::ivec3 centerChunkPos = world_pos_to_chunk_pos(glm::vec3(position.x, position.y, position.z));

    // do nothing if the center chunk hasn't changed
    if (loader.has_visited() && centerChunkPos == loader.lastVisitedChunk) return;

    loader.desiredChunks.clear();

    for (int x = -loader.loadRadius; x <= loader.loadRadius; x++) {
        for (int y = -loader.loadRadius; y <= loader.loadRadius; y++) {
            for (int z = -loader.loadRadius; z <= loader.loadRadius; z++) {
                glm::ivec3 chunkPos = centerChunkPos + glm::ivec3(x, y, z);
                loader.desiredChunks.insert(chunkPos);
            }
        }
    }

    loader.lastVisitedChunk = centerChunkPos;

    // update queues
    // find chunks that are desired but not loaded
    for (const auto& chunkPos : loader.desiredChunks) {
        if (!is_chunk_processed(chunkPos)) {
            // not loaded, add to load queue if not already loading
            if (!m_loadingChunks.contains(chunkPos)) {
                m_loadQueue.push_back(chunkPos);
                m_loadingChunks.insert(chunkPos);
            }
        }
    }

    // Find chunks to unload (loaded but outside unload radius)
    glm::ivec3 centerPos = loader.lastVisitedChunk;
    std::vector<glm::ivec3> chunksToUnload;

    for (auto& [chunkPos, chunkEntity] : m_loadedChunks) {
        // Check if this chunk is loaded by this loader
        //if (!chunkEntity.has<LoadedBy>(e)) continue;

        glm::ivec3 delta = chunkPos - centerPos;
        float distSq = delta.x*delta.x + delta.y*delta.y + delta.z*delta.z;

        if (distSq > loader.unloadRadius * loader.unloadRadius) {
            chunksToUnload.push_back(chunkPos);
        }
    }

    for (const auto& chunkPos : chunksToUnload) {
        if (std::ranges::find(m_unloadQueue, chunkPos) == m_unloadQueue.end()) {
            m_unloadQueue.push_back(chunkPos);
        }
    }
}

void ChunkManager::process_load_queue_system(flecs::iter &it) {
    WorldGenerator* generator = it.world().get_mut<WorldGenerator>();
    int chunksLoaded = 0;

    while (!m_loadQueue.empty() && chunksLoaded < MAX_CHUNKS_PER_FRAME) {
        glm::ivec3 chunkPos = m_loadQueue.front();
        m_loadQueue.erase(m_loadQueue.begin());

        // Skip if already processed (safety check)
        if (is_chunk_processed(chunkPos)) {
            continue;
        }

        VoxelChunk chunkData = {};
        if (generator->generate_chunk(chunkData, chunkPos)) {
            auto chunk = it.world().entity()
                .set<ChunkCoordinate>(chunkPos)
                .set<Position>({
                    static_cast<float>(chunkPos.x * CHUNK_SIZE),
                    static_cast<float>(chunkPos.y * CHUNK_SIZE),
                    static_cast<float>(chunkPos.z * CHUNK_SIZE)
                })
                .set<VoxelChunk>(chunkData);

            m_loadedChunks[chunkPos] = chunk;
        } else {
            m_emptyChunks.insert(chunkPos);
        }

        chunksLoaded++;
    }
}

void ChunkManager::process_unload_queue_system(flecs::iter &it) {
    int chunksUnloaded = 0;

    while (!m_unloadQueue.empty() && chunksUnloaded < MAX_CHUNKS_PER_FRAME) {
        glm::ivec3 chunkPos = m_unloadQueue.front();
        m_unloadQueue.erase(m_unloadQueue.begin());

        auto loadedIt = m_loadedChunks.find(chunkPos);
        if (loadedIt != m_loadedChunks.end()) {
            if (!is_chunk_still_needed(chunkPos, it)) {
                it.world().entity(loadedIt->second).destruct();
                m_loadedChunks.erase(loadedIt);
                chunksUnloaded++;
            }
        } else {
            auto emptyIt = m_emptyChunks.find(chunkPos);
            if (emptyIt != m_emptyChunks.end()) {
                if (!is_chunk_still_needed(chunkPos, it)) {
                    m_emptyChunks.erase(emptyIt);
                    chunksUnloaded++;
                }
            }
        }
    }
}

bool ChunkManager::is_chunk_still_needed(const glm::ivec3& chunkPos, flecs::iter &it) {
    bool stillNeeded = false;
    it.world().each<ChunkLoader>([&](flecs::entity e, ChunkLoader& loader) {
        if (loader.desiredChunks.contains(chunkPos)) {
            stillNeeded = true;
        }
    });
    return stillNeeded;
}

void ChunkManager::Register(flecs::world &ecs) {
    ecs.component<ChunkLoader>();

    ecs.set<ChunkManager>({});
    ecs.get_mut<ChunkManager>()->init(ecs);
}