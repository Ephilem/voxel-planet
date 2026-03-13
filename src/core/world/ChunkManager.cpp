#include "ChunkManager.h"

#include <algorithm>
#include <ranges>
#include <vector>

#include "WorldGenerator.h"
#include <imgui.h>
#include "core/log/Logger.h"
#include "platform/inputs/InputStateManager.h"
#include "renderer/rendering_components.h"

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

    ecs.system<ChunkLoader, const Position>("ChunkManager-UpdateChunksSystem")
            .kind(flecs::OnUpdate)
            .each([this](flecs::entity e, ChunkLoader &loader, const Position &position) {
                auto* generator = e.world().get_mut<WorldGenerator>();
                auto* inputManager = e.world().get_mut<InputActionState>();
                // if (!inputManager->is_action_pressed(ActionInputType::Debug1)) return;
                update_chunks_system(e, loader, position, generator);
            });

    ecs.system("ChunkManager-PollGenerationResults")
            .kind(flecs::OnStore)
            .run([this](flecs::iter &it) {
                poll_generation_results_system(it);
            });

    ecs.system("ChunkManager-ProcessUnloadQueue")
            .kind(flecs::OnStore)
            .run([this](flecs::iter &it) {
                process_unload_queue_system(it);
            });

    // Debug UI
    ecs.system("ChunkManager-DebugInfo")
            .kind(flecs::OnStore)
            .run([this](flecs::iter &it) {
                ImGui::Begin("Chunk Debug");
                ImGui::Text("Loading: %zu", m_loadingChunks.size());
                ImGui::Text("Loaded: %zu", m_loadedChunks.size());
                ImGui::Text("Empty: %zu", m_emptyChunks.size());
                ImGui::Text("Cancelled: %zu", m_cancelledChunks.size());
                ImGui::Text("Unload Queue: %zu", m_unloadQueue.size());
                {
                    std::lock_guard<std::mutex> lock(m_generationMutex);
                    ImGui::Text("Generation Queue: %zu", m_generationQueue.size());
                    ImGui::Text("Results Pending: %zu", m_generationResultsQueue.size());
                }
                ImGui::End();
            });

    // init threads
    size_t numThreads = std::max(1u, std::thread::hardware_concurrency() - 1);
    for (size_t i = 0; i < numThreads; i++) {
        m_generationThreads.emplace_back([this, i] { generation_worker_loop(i); });
    }

    LOG_INFO("ChunkManager", "Started {} generation worker threads", numThreads);
}

void ChunkManager::update_chunks_system(flecs::entity e, ChunkLoader &loader,
                                         const Position &position, WorldGenerator* generator) {
    glm::ivec3 currentChunk = world_pos_to_chunk_pos({position.x, position.y, position.z});

    if (loader.has_visited() && currentChunk == loader.lastVisitedChunk) {
        return;
    }

    LOG_DEBUG("ChunkManager", "Player moved to chunk ({}, {}, {})",
              currentChunk.x, currentChunk.y, currentChunk.z);

    loader.desiredChunks.clear();
    const int radius = loader.loadRadius;
    // const float radiusSq = static_cast<float>(radius * radius);

    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            for (int z = -radius; z <= radius; z++) {
                // float distSq = static_cast<float>(x * x + y * y + z * z);
                // if (distSq > radiusSq) continue;

                glm::ivec3 chunkPos = currentChunk + glm::ivec3(x, y, z);
                loader.desiredChunks.insert(chunkPos);
            }
        }
    }

    request_chunks_in_radius(currentChunk, radius, generator);

    // Cancel chunk that are not desired anymore
    std::vector<glm::ivec3> toCancel;
    for (const auto& chunkPos : m_loadingChunks) {
        if (!loader.desiredChunks.contains(chunkPos)) {
            toCancel.push_back(chunkPos);
        }
    }
    for (const auto& chunkPos : toCancel) {
        cancel_chunk_generation(chunkPos);
    }

    update_unload_queue(loader, currentChunk);

    loader.lastVisitedChunk = currentChunk;
}

void ChunkManager::request_chunks_in_radius(const glm::ivec3& center, int radius, WorldGenerator* generator) {
    std::vector<ChunkCandidate> candidates;

    for (int x = -radius; x <= radius; x++) {
        for (int y = -radius; y <= radius; y++) {
            for (int z = -radius; z <= radius; z++) {

                glm::ivec3 chunkPos = center + glm::ivec3(x, y, z);

                if (is_chunk_processed(chunkPos) || is_chunk_in_progress(chunkPos)) {
                    continue;
                }

                m_cancelledChunks.erase(chunkPos);

                float priority = calculate_priority(chunkPos, center);
                candidates.push_back({chunkPos, priority});
            }
        }
    }

    if (!candidates.empty()) {
        std::ranges::sort(candidates, [](const auto& a, const auto& b) {
            return a.priority < b.priority;
        });

        enqueue_chunks_generation(candidates, generator);
        LOG_DEBUG("ChunkManager", "Enqueued {} chunks for generation", candidates.size());
    }
}

float ChunkManager::calculate_priority(const glm::ivec3& chunkPos, const glm::ivec3& center) {
    glm::vec3 diff = glm::vec3(chunkPos - center);
    float distSq = glm::dot(diff, diff);

    // Give a bonus to chunks at player height (similar Y)
    if (center.y - 1 <= chunkPos.y && chunkPos.y <= center.y + 1) {
        distSq *= 0.5f;
    }

    return distSq;
}

void ChunkManager::enqueue_chunks_generation(const std::vector<ChunkCandidate>& candidates, WorldGenerator* generator) {
    std::lock_guard<std::mutex> lock(m_generationMutex);

    for (const auto& candidate : candidates) {
        m_loadingChunks.insert(candidate.pos);
        m_generationQueue.push(TaskGeneratingInput{
            .chunkCoord = candidate.pos,
            .generator = generator,
            .priority = candidate.priority
        });
    }

    m_generationCv.notify_all();
}

void ChunkManager::cancel_chunk_generation(const glm::ivec3& chunkPos) {
    // We can't reliably remove from the priority queue, so we just mark it as cancelled
    m_cancelledChunks.insert(chunkPos);
    m_loadingChunks.erase(chunkPos);
}

void ChunkManager::update_unload_queue(const ChunkLoader& loader, const glm::ivec3& centerChunk) {
    const float unloadRadiusSq = static_cast<float>(loader.unloadRadius * loader.unloadRadius);

    for (const auto& [chunkPos, entity] : m_loadedChunks) {
        glm::vec3 diff = glm::vec3(chunkPos - centerChunk);
        float distSq = glm::dot(diff, diff);

        if (distSq > unloadRadiusSq) {
            if (std::ranges::find(m_unloadQueue, chunkPos) == m_unloadQueue.end()) {
                m_unloadQueue.push_back(chunkPos);
            }
        }
    }

    std::vector<glm::ivec3> emptyToRemove;
    for (const auto& chunkPos : m_emptyChunks) {
        glm::vec3 diff = glm::vec3(chunkPos - centerChunk);
        float distSq = glm::dot(diff, diff);

        if (distSq > unloadRadiusSq) {
            emptyToRemove.push_back(chunkPos);
        }
    }
    for (const auto& pos : emptyToRemove) {
        m_emptyChunks.erase(pos);
    }

    std::vector<glm::ivec3> cancelledToRemove;
    for (const auto& chunkPos : m_cancelledChunks) {
        glm::vec3 diff = glm::vec3(chunkPos - centerChunk);
        float distSq = glm::dot(diff, diff);

        if (distSq > unloadRadiusSq) {
            cancelledToRemove.push_back(chunkPos);
        }
    }
    for (const auto& pos : cancelledToRemove) {
        m_cancelledChunks.erase(pos);
    }
}

void ChunkManager::process_unload_queue_system(flecs::iter &it) {
    int chunksUnloaded = 0;

    while (!m_unloadQueue.empty() && chunksUnloaded < MAX_UNLOADS_PER_FRAME) {
        glm::ivec3 chunkPos = m_unloadQueue.front();
        m_unloadQueue.erase(m_unloadQueue.begin());

        auto loadedIt = m_loadedChunks.find(chunkPos);
        if (loadedIt != m_loadedChunks.end()) {
            if (!is_chunk_still_needed(chunkPos, it.world())) {
                loadedIt->second.destruct();
                m_loadedChunks.erase(loadedIt);
                chunksUnloaded++;
            }
        }
    }
}

bool ChunkManager::is_chunk_still_needed(const glm::ivec3 &chunkPos, const flecs::world &world) const {
    bool stillNeeded = false;
    world.each<ChunkLoader>([&](flecs::entity e, const ChunkLoader &loader) {
        if (loader.desiredChunks.contains(chunkPos)) {
            stillNeeded = true;
        }
    });
    return stillNeeded;
}

void ChunkManager::poll_generation_results_system(flecs::iter &it) {
    auto results = poll_generation_results(MAX_GENERATION_RESULTS_PER_FRAME);

    for (auto &result : results) {
        m_loadingChunks.erase(result.chunkCoord);

        if (m_cancelledChunks.contains(result.chunkCoord)) {
            m_cancelledChunks.erase(result.chunkCoord);
            continue;
        }

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

void ChunkManager::Register(flecs::world &ecs) {
    ecs.component<ChunkLoader>();
    ecs.emplace<ChunkManager>();
    ecs.get_mut<ChunkManager>()->init(ecs);
}

std::array<flecs::entity, 6> ChunkManager::get_neighboring_chunks(const glm::ivec3 &chunkPos) const {
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
    }};

    int i = 0;
    while (i < neighborOffsets.size()) {
        const auto &offset = neighborOffsets[i];
        glm::ivec3 neighborPos = chunkPos + offset;
        auto it = m_loadedChunks.find(neighborPos);
        if (it != m_loadedChunks.end()) {
            neighbors[i] = it->second;
        } else {
            neighbors[i] = flecs::entity::null();
        }
        i++;
    }

    return neighbors;
}

flecs::entity ChunkManager::get_chunk_entity(const glm::ivec3 &chunkPos) const {
    auto it = m_loadedChunks.find(chunkPos);
    if (it != m_loadedChunks.end()) {
        return it->second;
    }
    return flecs::entity::null();
}

bool ChunkManager::can_mesh(const glm::ivec3 &chunkPos) const {
    if (!m_loadedChunks.contains(chunkPos)) {
        return false;
    }

    static constexpr std::array<glm::ivec3, 6> neighborOffsets = {{
        {1, 0, 0}, {-1, 0, 0},
        {0, 1, 0}, {0, -1, 0},
        {0, 0, 1}, {0, 0, -1}
    }};

    for (const auto &offset: neighborOffsets) {
        glm::ivec3 neighborPos = chunkPos + offset;
        if (m_loadingChunks.contains(neighborPos)) {
            return false;
        }
    }

    return true;
}

std::vector<TaskGeneratingOutput> ChunkManager::poll_generation_results(size_t maxResults) {
    std::vector<TaskGeneratingOutput> results;
    results.reserve(maxResults);

    std::lock_guard<std::mutex> lock(m_generationMutex);
    while (!m_generationResultsQueue.empty() && results.size() < maxResults) {
        results.push_back(std::move(m_generationResultsQueue.front()));
        m_generationResultsQueue.pop();
    }

    return results;
}

void ChunkManager::generation_worker_loop(size_t id) {
    while (true) {
        TaskGeneratingInput input;
        {
            std::unique_lock<std::mutex> lock(m_generationMutex);
            m_generationCv.wait(lock, [this] {
                return m_stopGeneration || !m_generationQueue.empty();
            });

            if (m_stopGeneration && m_generationQueue.empty()) {
                return;
            }

            input = std::move(m_generationQueue.top());
            m_generationQueue.pop();
        }

        // Vérifier si annulé AVANT de générer (évite le travail inutile)
        if (m_cancelledChunks.contains(input.chunkCoord)) {
            continue;
        }

        TaskGeneratingOutput result{};
        VoxelChunk chunkData = {};
        result.empty = !input.generator->generate_chunk(chunkData, input.chunkCoord);
        result.success = true;
        result.chunkCoord = input.chunkCoord;
        result.voxels = std::move(chunkData.voxels);
        result.textureIDs = std::move(chunkData.textureIDs);

        {
            std::lock_guard<std::mutex> lock(m_generationMutex);
            m_generationResultsQueue.push(std::move(result));
        }
    }
}