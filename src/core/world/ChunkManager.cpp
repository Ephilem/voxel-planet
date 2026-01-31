#include "ChunkManager.h"

#include <algorithm>
#include <ranges>
#include <vector>

#include "WorldGenerator.h"
#include "../../../cmake-build-debug/_deps/imgui-src/imgui.h"
#include "core/log/Logger.h"
#include "platform/inputs/input_state.h"

struct InputActionState;

ChunkManager::~ChunkManager() {
    shutdown();
}

void ChunkManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_generationMutex);
        m_stopGeneration = true;
    }
    m_generationCv.notify_all();

    for (auto &thread: m_generationThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_generationThreads.clear();
    LOG_INFO("ChunkManager", "All generation worker threads shut down");
}

void ChunkManager::init(flecs::world &ecs) {
    ecs.component<VoxelChunkState>()
            .add(flecs::Exclusive);

    // register systems
    ecs.system<ChunkLoader, const Position>("ChunkManager-UpdateLoadQueueSystem")
            .kind(flecs::OnUpdate)
            .each([this](flecs::entity e, ChunkLoader &loader, const Position &position) {
                const auto* inputState = e.world().get<InputActionState>();
                auto* generator = e.world().get_mut<WorldGenerator>();
                if (inputState->is_action_pressed(ActionInputType::Debug1)) {
                    this->load_chunks_at_radius({0, 0, 0}, 8, generator);
                }

                ImGui::Begin("Chunk Debug");
                ImGui::Text("Load Queue: %zu", m_loadQueue.size());
                ImGui::Text("Loading: %zu", m_loadingChunks.size());
                ImGui::Text("Loaded: %zu", m_loadedChunks.size());
                ImGui::Text("Empty: %zu", m_emptyChunks.size());
                ImGui::Text("Unload Queue: %zu", m_unloadQueue.size());

                // For generation queue, you need the mutex (or add an atomic counter)
                {
                    std::lock_guard<std::mutex> lock(m_generationMutex);
                    ImGui::Text("Generation Queue: %zu", m_generationQueue.size());
                    ImGui::Text("Results Pending: %zu", m_generationResultsQueue.size());
                }
                ImGui::End();
            });

    ecs.system("ChunkManager-ProcessLoadQueue")
            .kind(flecs::OnStore)
            .run([this](flecs::iter &it) {
                this->process_load_queue_system(it);
            });

    ecs.system("ChunkManager-PollGenerationResults")
            .kind(flecs::OnStore)
            .run([this](flecs::iter &it) {
                this->poll_generation_results_system(it);
            });

    ecs.system("ChunkManager-ProcessUnloadQueue")
            .kind(flecs::OnStore)
            .run([this](flecs::iter &it) {
                this->process_unload_queue_system(it);
            });

    // init threads
    size_t numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    for (size_t i = 0; i < numThreads; i++) {
        m_generationThreads.emplace_back([this, i] { generation_worker_loop(i); });
    }
}

void ChunkManager::load_chunks_at_radius(const ChunkCoordinate &center, int radius, WorldGenerator* generator) {
    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            for (int z = -radius; z <= radius; z++) {
                glm::ivec3 chunkPos = glm::ivec3(center.x + x, center.y + y, center.z + z);
                if (!is_chunk_processed(chunkPos) && !m_loadingChunks.contains(chunkPos)) {
                    enqueue_chunk_generation(chunkPos, generator);
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
    for (const auto &chunkPos: loader.desiredChunks) {
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

    for (auto &[chunkPos, chunkEntity]: m_loadedChunks) {
        // Check if this chunk is loaded by this loader
        //if (!chunkEntity.has<LoadedBy>(e)) continue;

        glm::ivec3 delta = chunkPos - centerPos;
        float distSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;

        if (distSq > loader.unloadRadius * loader.unloadRadius) {
            chunksToUnload.push_back(chunkPos);
        }
    }

    for (const auto &chunkPos: chunksToUnload) {
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

bool ChunkManager::is_chunk_still_needed(const glm::ivec3 &chunkPos, flecs::iter &it) {
    bool stillNeeded = false;
    it.world().each<ChunkLoader>([&](flecs::entity e, ChunkLoader &loader) {
        if (loader.desiredChunks.contains(chunkPos)) {
            stillNeeded = true;
        }
    });
    return stillNeeded;
}

void ChunkManager::Register(flecs::world &ecs) {
    ecs.component<ChunkLoader>();

    ecs.emplace<ChunkManager>();
    ecs.get_mut<ChunkManager>()->init(ecs);
}

void ChunkManager::poll_generation_results_system(flecs::iter &it) {
    auto results = poll_generation_results(30);

    for (auto &result: results) {
        m_loadingChunks.erase(result.chunkCoord);
        if (!result.success) continue;

        if (result.voxels && !result.empty) {
            VoxelChunk chunkData = {};
            chunkData.voxels = std::move(result.voxels);
            chunkData.textureIDs = std::move(result.textureIDs);

            auto chunk = it.world().entity()
                    .set<ChunkCoordinate>(result.chunkCoord)
                    .set<Position>({
                        static_cast<float>(result.chunkCoord.x * CHUNK_SIZE),
                        static_cast<float>(result.chunkCoord.y * CHUNK_SIZE),
                        static_cast<float>(result.chunkCoord.z * CHUNK_SIZE)
                    })
                    .set<VoxelChunk>(chunkData);

            m_loadedChunks[result.chunkCoord] = chunk;
        } else {
            m_emptyChunks.insert(result.chunkCoord);
        }
    }
}

void ChunkManager::enqueue_chunk_generation(const glm::ivec3 &chunkPos, WorldGenerator* generator) {
    m_loadingChunks.insert(chunkPos);
    {
        std::lock_guard<std::mutex> lock(m_generationMutex);
        m_generationQueue.push(TaskGeneratingInput{
            .chunkCoord = chunkPos,
            .generator = generator
        });
    }
    m_generationCv.notify_one();
}

std::vector<TaskGeneratingOutput> ChunkManager::poll_generation_results(size_t maxResults) {
    std::vector<TaskGeneratingOutput> results = {};

    std::lock_guard<std::mutex> lock(m_generationMutex);
    while (!m_generationResultsQueue.empty() && results.size() < maxResults) {
        TaskGeneratingOutput output = std::move(m_generationResultsQueue.front());
        m_generationResultsQueue.pop();
        results.push_back(std::move(output));
    }

    return results;
}

void ChunkManager::generation_worker_loop(size_t id) {
    while (true) {
        TaskGeneratingInput input; {
            std::unique_lock<std::mutex> lock(m_generationMutex);
            m_generationCv.wait(lock, [this] {
                return m_stopGeneration || !m_generationQueue.empty();
            });

            if (m_stopGeneration && m_generationQueue.empty()) {
                return;
            }

            input = std::move(m_generationQueue.front());
            m_generationQueue.pop();
        }

        TaskGeneratingOutput result{};
        VoxelChunk chunkData = {};
        result.empty = !input.generator->generate_chunk(chunkData, input.chunkCoord);
        result.success = true;
        result.chunkCoord = input.chunkCoord;
        result.voxels = std::move(chunkData.voxels);
        result.textureIDs = std::move(chunkData.textureIDs); {
            std::lock_guard<std::mutex> lock(m_generationMutex);
            m_generationResultsQueue.push(std::move(result));
        }
    }
}
