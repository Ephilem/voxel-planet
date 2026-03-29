#include "ChunkManager.h"

#include <algorithm>
#include <ranges>
#include <vector>

#include "WorldGenerator.h"
#include <imgui.h>

#include "core/TracyIntegration.h"
#include "core/log/Logger.h"
#include "platform/inputs/InputStateManager.h"

ChunkManager::~ChunkManager() {
    shutdown();
}

void ChunkManager::shutdown() {
    m_stopGeneration = true;
    m_generationSemaphore.release(m_generationThreads.size());

    for (auto &thread: m_generationThreads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    m_generationThreads.clear();
    LOG_INFO("ChunkManager", "All generation worker threads shut down");
}

ChunkManagerStats ChunkManager::get_stats() const {
    ChunkManagerStats stats{};
    stats.loadedChunkCount = m_loadedChunks.size();
    stats.loadingChunkCount = m_loadingChunks.size();
    stats.emptyChunkCount = m_emptyChunks.size();
    stats.candidateChunkCount = m_candidateHeap.size();
    stats.unloadChunkCount = m_unloadQueue.size();

    stats.chunksGenerated = m_chunksGenerated.load(std::memory_order_relaxed);
    stats.chunksUnloaded = m_chunksUnloaded.load(std::memory_order_relaxed);

    return stats;
}

ChunkState ChunkManager::get_chunk_state(const ChunkKey &key) const {
    if (m_loadedChunks.contains(key)) return ChunkState::Loaded;
    if (m_emptyChunks.contains(key)) return ChunkState::Empty;
    if (m_loadingChunks.contains(key)) return ChunkState::Loading;
    if (m_inCandidateHeap.contains(key)) return ChunkState::Candidate;
    if (m_cancelledChunks.contains(key)) return ChunkState::Cancelled;
    return ChunkState::None;
}

void ChunkManager::unload_all_chunks(flecs::world &ecs) {
    for (auto &[key, entity]: m_loadedChunks) {
        entity.destruct();
    }
    ecs.each([](ChunkLoader &loader) {
        loader.lastVisitedChunk = glm::ivec3(INT32_MAX);
    });

    m_loadedChunks.clear();
    m_emptyChunks.clear();
    m_loadingChunks.clear();
    m_cancelledChunks.clear();
    m_inCandidateHeap.clear();
    m_candidateHeap.clear();
    m_unloadQueue.clear();
    m_unloadQueueSet.clear();

    LOG_INFO("ChunkManager", "All chunks unloaded with ecs");
}

void ChunkManager::init(flecs::world &ecs) {
    ecs.component<VoxelChunkState>()
            .add(flecs::Exclusive);

    ecs.system<ChunkLoader, const Position>("ChunkManager-UpdateChunksSystem")
            .kind(flecs::OnUpdate)
            .each([this](flecs::entity e, ChunkLoader &loader, const Position &position) {
                VOXEL_ZONE_N("ChunkManager-UpdateChunks")
                auto *generator = e.world().get_mut<WorldGenerator>();
                auto *inputManager = e.world().get_mut<InputActionState>();
                // if (!inputManager->is_action_pressed(ActionInputType::Debug1)) return;
                update_chunks_system(e, loader, position, generator);
            });

    ecs.system("ChunkManager-DrainCandidateBuffer")
            .kind(flecs::OnUpdate)
            .run([this](flecs::iter &it) {
                VOXEL_ZONE_N("ChunkManager-DrainCandidates");
                drain_candidate_buffer_system();
            });

    ecs.system("ChunkManager-PollGenerationResults")
            .kind(flecs::OnStore)
            .run([this](flecs::iter &it) {
                VOXEL_ZONE_N("ChunkManager-PollResults");
                poll_generation_results_system(it);
            });

    ecs.system("ChunkManager-ProcessUnloadQueue")
            .kind(flecs::OnStore)
            .run([this](flecs::iter &it) {
                VOXEL_ZONE_N("ChunkManager-UnloadQueue");
                process_unload_queue_system(it);
            });

    // init threads
    size_t numThreads = std::max(1u, std::thread::hardware_concurrency() / 2);
    // size_t numThreads = 2;

    m_workerResults.reserve(numThreads);
    for (size_t i = 0; i < numThreads; i++) {
        m_workerResults.push_back(std::make_unique<GenerationWorkerResult>());
    }
    for (size_t i = 0; i < numThreads; i++) {
        m_generationThreads.emplace_back([this, i] { generation_worker_loop(i); });
    }

    LOG_INFO("ChunkManager", "Started {} generation worker threads", numThreads);
}

void ChunkManager::update_chunks_system(flecs::entity e, ChunkLoader &loader,
                                        const Position &position, WorldGenerator *generator) {
    const Clipmap clipmap = Clipmap::from_radius(loader.loadRadius, 8);

    glm::ivec3 currentChunk = world_pos_to_chunk_pos({position.x, position.y, position.z});

    if (loader.has_visited() && currentChunk == loader.lastVisitedChunk) {
        return;
    }

    VOXEL_MESSAGE("MOVED CHUNK");
    LOG_DEBUG("ChunkManager", "Player moved to chunk ({}, {}, {})",
              currentChunk.x, currentChunk.y, currentChunk.z);

    const int radius = loader.loadRadius;
    // Save old center before updating — used for delta and must be safe (no INT32_MAX overflow)
    const glm::ivec3 oldCenter = loader.has_visited()
                                     ? loader.lastVisitedChunk
                                     : currentChunk + glm::ivec3(radius * 3 + 1);

    // Update early so is_chunk_desired uses the correct center in the cancel loop below
    loader.lastVisitedChunk = currentChunk;
    m_currentCenter = currentChunk;
    if (!enqueueCandidates) return;

    request_chunks_in_radius(currentChunk, oldCenter, radius, generator);

    // Cancel chunks that are no longer desired
    {
        VOXEL_ZONE_N("UpdateChunks-CancelOutOfRange");
        std::vector<ChunkKey> toCancel;
        for (const auto &key : m_loadingChunks) {
            glm::ivec3 diff = key.pos - currentChunk;
            int maxComp = std::max({std::abs(diff.x), std::abs(diff.y), std::abs(diff.z)});
            if (maxComp > clipmap.outerRadius(key.lod))
                toCancel.push_back(key);
        }
        for (const auto &key : toCancel)
            cancel_chunk_generation(key);
    }
    {
        VOXEL_ZONE_N("UpdateChunks-UnloadQueue");
        update_unload_queue(loader, currentChunk);
    }
}

void ChunkManager::request_chunks_in_radius(const glm::ivec3 &center, const glm::ivec3 &oldCenter, int radius,
                                            WorldGenerator *generator) {
    VOXEL_ZONE_N("RequestChunksInRadius");

    m_currentCenter = center;
    m_currentLoadRadius = radius;
    m_cachedGenerator = generator;

    if (!m_candidateHeap.empty()) {
        VOXEL_ZONE_N("RequestChunks-UpdatePriorities");
        for (auto &c: m_candidateHeap)
            c.priority = calculate_priority(c.key, center);
    }

    std::vector<ChunkCandidate> newCandidates;

    const Clipmap clipmap = Clipmap::from_radius(radius, 8);

    {
        VOXEL_ZONE_N("RequestChunks-IterateAndFilter");

        for (int lod = 0; lod < clipmap.numLods; lod++) {
            const int inner = clipmap.innerRadius(lod);
            const int outer = clipmap.outerRadius(lod);
            const int step  = clipmap.step(lod);

            for (int x = -outer; x < outer; x += step)
                for (int z = -outer; z < outer; z += step)
                    for (int y = -outer; y < outer; y += step) {
                        // Skip inner region
                        if (std::max({std::abs(x), std::abs(y), std::abs(z)}) <= inner) continue;

                        ChunkKey key{center + glm::ivec3(x, y, z), lod};
                        if (is_chunk_processed(key) || is_chunk_in_progress(key)) continue;

                        m_cancelledChunks.erase(key);
                        newCandidates.push_back({key, calculate_priority(key, center)});
                    }
        }
    }

    for (const auto &c: newCandidates) {
        m_candidateHeap.push_back(c);
        m_inCandidateHeap.insert(c.key);
    }

    if (!m_candidateHeap.empty())
        std::make_heap(m_candidateHeap.begin(), m_candidateHeap.end(), std::greater<ChunkCandidate>{});

    LOG_DEBUG("ChunkManager", "Added {} candidates to heap (total: {})", newCandidates.size(), m_candidateHeap.size());
}

float ChunkManager::calculate_priority(const ChunkKey &key, const glm::ivec3 &center) {
    glm::vec3 diff = glm::vec3(key.pos - center);
    float distSq = glm::dot(diff, diff);
    if (center.y - 1 <= key.pos.y && key.pos.y <= center.y + 1)
        distSq *= 0.5f;
    return distSq;
}

void ChunkManager::drain_candidate_buffer_system() {
    if (!loadingEnabled) return;

    if (m_candidateHeap.empty() || !m_cachedGenerator) return;

    auto startTime = std::chrono::steady_clock::now();
    std::unordered_map<int64_t, int> columnCache;
    std::vector<TaskGeneratingInput> toEnqueue;

    while (!m_candidateHeap.empty()) {
        if (!toEnqueue.empty() && std::chrono::steady_clock::now() - startTime >= DRAIN_TIME_BUDGET)
            break;

        std::pop_heap(m_candidateHeap.begin(), m_candidateHeap.end(), std::greater<ChunkCandidate>{});
        ChunkCandidate candidate = m_candidateHeap.back();
        m_candidateHeap.pop_back();
        m_inCandidateHeap.erase(candidate.key);

        if (is_chunk_processed(candidate.key) || m_loadingChunks.contains(candidate.key))
            continue;

        glm::ivec3 diff = candidate.key.pos - m_currentCenter;
        if (std::max({std::abs(diff.x), std::abs(diff.y), std::abs(diff.z)}) > m_currentLoadRadius)
            continue;

        if (m_cancelledChunks.contains(candidate.key)) {
            m_cancelledChunks.erase(candidate.key);
            continue;
        }

        // int64_t colKey = ((int64_t) candidate.key.pos.x << 32) | (uint32_t) candidate.key.pos.z;
        // auto [colIt, inserted] = columnCache.emplace(colKey, 0);
        // if (inserted) {
        //     int yMax = m_cachedGenerator->evaluate_column({candidate.key.pos.x, candidate.key.pos.z}).yMax;
        //     colIt->second = (yMax + CHUNK_SIZE - 1) / CHUNK_SIZE;
        // }
        // int surfaceChunkY = colIt->second;
        //
        // if (candidate.key.pos.y >= surfaceChunkY)
        //     continue;

        // float depth = static_cast<float>(std::max(0, surfaceChunkY - candidate.key.pos.y));
        // float priority = candidate.priority + depth * 4.0f;

        m_loadingChunks.insert(candidate.key);
        toEnqueue.push_back({
            .chunkCoord = candidate.key.pos, .generator = m_cachedGenerator, .lod = candidate.key.lod,
            .priority = candidate.priority
        });
    }

    if (!toEnqueue.empty()) {
        std::lock_guard lock(m_generationMutex);
        for (auto &task: toEnqueue)
            m_generationQueue.push_back(std::move(task));
        std::make_heap(m_generationQueue.begin(), m_generationQueue.end(), std::greater<TaskGeneratingInput>{});
        m_generationSemaphore.release(
            std::min((toEnqueue.size() + GENERATION_BATCH_SIZE - 1) / GENERATION_BATCH_SIZE,
                     static_cast<size_t>(3)));
    }
}

void ChunkManager::cancel_chunk_generation(const ChunkKey &key) {
    // We can't reliably remove from the priority queue, so we just mark it as cancelled
    m_cancelledChunks.insert(key);
    m_loadingChunks.erase(key);
}

void ChunkManager::update_unload_queue(const ChunkLoader &loader, const glm::ivec3 &centerChunk) {
    const Clipmap clipmap = Clipmap::from_radius(loader.loadRadius, 8);

    auto is_out_of_range = [&](const ChunkKey &key) {
        glm::ivec3 diff = key.pos - centerChunk;
        int maxComp = std::max({std::abs(diff.x), std::abs(diff.y), std::abs(diff.z)});
        return maxComp > clipmap.unloadRadius(key.lod);
    };

    for (const auto &[key, entity] : m_loadedChunks) {
        if (is_out_of_range(key) && m_unloadQueueSet.insert(key).second)
            m_unloadQueue.push_back(key);
    }

    std::erase_if(m_emptyChunks,     [&](const ChunkKey &k){ return is_out_of_range(k); });
    std::erase_if(m_cancelledChunks, [&](const ChunkKey &k){ return is_out_of_range(k); });
    std::erase_if(m_inCandidateHeap, [&](const ChunkKey &k){ return is_out_of_range(k); });
}

void ChunkManager::process_unload_queue_system(flecs::iter &it) {
    if (!unloadQueueEnabled) return;
    int chunksUnloaded = 0;

    while (!m_unloadQueue.empty() && chunksUnloaded < MAX_UNLOADS_PER_FRAME) {
        ChunkKey key = m_unloadQueue.front();
        m_unloadQueue.pop_front();
        m_unloadQueueSet.erase(key);

        auto loadedIt = m_loadedChunks.find(key);
        if (loadedIt != m_loadedChunks.end()) {
            if (!is_chunk_still_needed(key, it.world())) {
                loadedIt->second.destruct();
                m_loadedChunks.erase(loadedIt);
                chunksUnloaded++;
            }
        }
    }
    m_chunksUnloaded.fetch_add(chunksUnloaded, std::memory_order_relaxed);
}

bool ChunkManager::is_chunk_still_needed(const ChunkKey &key, const flecs::world &world) const {
    bool stillNeeded = false;
    world.each<ChunkLoader>([&](flecs::entity e, const ChunkLoader &loader) {
        if (loader.is_chunk_desired(key.pos)) {
            stillNeeded = true;
        }
    });
    return stillNeeded;
}

void ChunkManager::poll_generation_results_system(flecs::iter &it) {
    auto results = poll_generation_results(MAX_GENERATION_RESULTS_PER_FRAME);

    for (auto &result: results) {
        VOXEL_ZONE_N("HandleGenerationResult");
        ChunkKey key{result.chunkCoord, result.lod};
        m_loadingChunks.erase(key);

        if (m_cancelledChunks.contains(key)) {
            m_cancelledChunks.erase(key);
            continue;
        }

        if (!result.success) continue;

        if (result.voxels && !result.empty) {
            VoxelChunk chunkData = {};
            chunkData.voxels = std::move(result.voxels);
            chunkData.textureIDs = std::move(result.textureIDs);
            chunkData.lod = result.lod;

            flecs::entity chunk; {
                VOXEL_ZONE_N("CreateChunkEntity");
                chunk = it.world().entity()
                        .set<ChunkCoordinate>(result.chunkCoord)
                        .set<Position>({
                            static_cast<float>(result.chunkCoord.x * CHUNK_SIZE),
                            static_cast<float>(result.chunkCoord.y * CHUNK_SIZE),
                            static_cast<float>(result.chunkCoord.z * CHUNK_SIZE)
                        })
                        .set<VoxelChunk>(chunkData);
            }

            m_loadedChunks[key] = chunk;
        } else {
            m_emptyChunks.insert(key);
        }
        m_chunksGenerated.fetch_add(1, std::memory_order_relaxed);
    }
}

void ChunkManager::Register(flecs::world &ecs) {
    ecs.component<ChunkLoader>();
    ecs.emplace<ChunkManager>();
    ecs.get_mut<ChunkManager>()->init(ecs);
}

std::array<flecs::entity, 6> ChunkManager::get_neighboring_chunks(const glm::ivec3 &chunkPos, uint8_t lod) const {
    std::array<flecs::entity, 6> neighbors;

    static constexpr std::array<glm::ivec3, 6> neighborOffsets = {
        {
            // +X
            {1, 0, 0},
            // -X
            {-1, 0, 0},
            // +Y
            {0, 1, 0},
            // -Y
            {0, -1, 0},
            // +Z
            {0, 0, 1},
            // -Z
            {0, 0, -1}
        }
    };

    int i = 0;
    while (i < neighborOffsets.size()) {
        const auto &offset = neighborOffsets[i] * (1 << lod);
        glm::ivec3 neighborPos = chunkPos + offset;
        auto it = m_loadedChunks.find(ChunkKey{neighborPos, lod});
        if (it != m_loadedChunks.end()) {
            neighbors[i] = it->second;
        } else {
            neighbors[i] = flecs::entity::null();
        }
        i++;
    }

    return neighbors;
}

flecs::entity ChunkManager::get_chunk_entity(const ChunkKey &key) const {
    auto it = m_loadedChunks.find(key);
    if (it != m_loadedChunks.end()) {
        return it->second;
    }
    return flecs::entity::null();
}

bool ChunkManager::can_mesh(const glm::ivec3 &chunkPos, uint8_t lod) const {
    VOXEL_ZONE_N("Can Mesh Test");
    // if (lod > 0) return true;
    if (!m_loadedChunks.contains(ChunkKey{chunkPos, lod})) {
        return false;
    }

    static constexpr std::array<glm::ivec3, 6> neighborOffsets = {
        {
            {1, 0, 0}, {-1, 0, 0},
            {0, 1, 0}, {0, -1, 0},
            {0, 0, 1}, {0, 0, -1}
        }
    };

    for (const auto &offset: neighborOffsets) {
        glm::ivec3 neighborPos = chunkPos + offset * (1 << lod);
        if (m_loadingChunks.contains(ChunkKey{neighborPos, lod})) {
            return false;
        }
    }

    return true;
}

std::vector<TaskGeneratingOutput> ChunkManager::poll_generation_results(size_t maxResults) {
    std::vector<TaskGeneratingOutput> results;
    results.reserve(maxResults);

    for (auto &wr: m_workerResults) {
        VOXEL_ZONE_N("DrainWorkerResults");
        if (results.size() >= maxResults) break;
        if (wr->pendingCount.load(std::memory_order_relaxed) == 0) continue;

        std::lock_guard lock(wr->m_resultMutex);
        int count = static_cast<int>(wr->results.size());
        for (auto &r: wr->results) {
            results.push_back(std::move(r));
        }
        wr->results.clear();
        wr->pendingCount.fetch_sub(count, std::memory_order_relaxed);
    }

    return results;
}

void ChunkManager::generation_worker_loop(size_t id) {
#ifdef TRACY_ENABLE
    char threadName[32];
    snprintf(threadName, sizeof(threadName), "ChunkGenWorker %zu", id);
    tracy::SetThreadName(threadName);
#endif

    std::vector<TaskGeneratingInput> batch;
    std::vector<TaskGeneratingOutput> results;
    batch.reserve(GENERATION_BATCH_SIZE);
    results.reserve(GENERATION_BATCH_SIZE);

    while (true) {
        batch.clear();

        m_generationSemaphore.acquire();

        if (m_stopGeneration && [&] {
            std::lock_guard lock(m_generationMutex);
            return m_generationQueue.empty();
        }())
            return; {
            VOXEL_ZONE_N("PollJobs");
            std::lock_guard lock(m_generationMutex);
            while (batch.size() < GENERATION_BATCH_SIZE && !m_generationQueue.empty()) {
                std::pop_heap(m_generationQueue.begin(), m_generationQueue.end(), std::greater<TaskGeneratingInput>{});
                batch.push_back(std::move(m_generationQueue.back()));
                m_generationQueue.pop_back();
            }
            if (!m_generationQueue.empty())
                m_generationSemaphore.release(1);
        }

        if (batch.empty()) continue; {
            VOXEL_ZONE_N("ProcessBatch");
            results.clear();
            for (auto &input: batch) {
                {
                    VOXEL_ZONE_N("CheckCancelled");
                    if (m_cancelledChunks.contains(ChunkKey{input.chunkCoord, input.lod})) continue;
                }

                TaskGeneratingOutput result{};
                VoxelChunk chunkData = {}; {
                    VOXEL_ZONE_N("GenerateChunk");
                    result.empty = !input.generator->generate_chunk(chunkData, input.chunkCoord, input.lod);
                }
                result.success = true;
                result.chunkCoord = input.chunkCoord;
                result.lod = input.lod;
                result.voxels = std::move(chunkData.voxels);
                result.textureIDs = std::move(chunkData.textureIDs);
                results.push_back(std::move(result));
            }
        } {
            VOXEL_ZONE_N("PushResults");
            auto &wr = *m_workerResults[id];
            std::lock_guard lock(wr.m_resultMutex);
            for (auto &result: results) {
                wr.results.push_back(std::move(result));
            }
            wr.pendingCount.fetch_add(static_cast<int>(results.size()), std::memory_order_relaxed);
        }
    }
}
