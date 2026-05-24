//
// Created by raph on 17/04/2026.
//

#include "PlanetChunkGenerator.h"

#include "PlanetWorldGenerator.h"
#include "core/log/Logger.h"

using namespace vp;

PlanetChunkGenerator::PlanetChunkGenerator() {
    size_t threadCount = std::thread::hardware_concurrency() / 2;
    m_generationWorkerResults.reserve(threadCount);
    for (size_t i = 0; i < threadCount; i++) {
        m_generationWorkerResults.push_back(std::make_unique<GenerationWorkerResult>());
        m_generationThreads.emplace_back(&PlanetChunkGenerator::worker_loop, this, i);
    }
    LOG_TRACE("PlanetChunkGenerator", "Started {} generation worker threads", threadCount);
}

PlanetChunkGenerator::~PlanetChunkGenerator() {
    m_stopGeneration = true;
    m_generationSemaphore.release(static_cast<int>(m_generationThreads.size()));
    for (auto &thread: m_generationThreads) {
        thread.join();
    }
    LOG_TRACE("PlanetChunkGenerator", "All generation worker threads stopped");
}

void PlanetChunkGenerator::enqueue(const ChunkGenInput &input) {
    enqueues(&input, 1);
}

void PlanetChunkGenerator::enqueues(const ChunkGenInput *inputs, size_t count) {
    if (count == 0) return; {
        std::lock_guard lock(m_enqueueGenerationMutex);
        for (auto i = 0; i < count; i++) {
            m_generationQueue.push_back(inputs[i]);
        }
        std::make_heap(m_generationQueue.begin(), m_generationQueue.end(),
                       [](const ChunkGenInput &a, const ChunkGenInput &b) {
                           return a.priority > b.priority; // max-heap
                       });
        m_generationSemaphore.release(
            std::min((count + GENERATION_BATCH_SIZE - 1) / GENERATION_BATCH_SIZE, static_cast<size_t>(3)));
    }
}

std::vector<ChunkGenOutput> PlanetChunkGenerator::poll_results(size_t max) {
    std::vector<ChunkGenOutput> all;
    for (auto &worker: m_generationWorkerResults) {
        if (worker->pendingCount.load() == 0) continue;
        std::lock_guard lock(worker->m_resultMutex);
        for (auto &r: worker->results) {
            all.push_back(std::move(r));
            if (max > 0 && all.size() >= max) break;
        }
        worker->results.clear();
        worker->pendingCount.store(0);
    }
    return all;
}

void PlanetChunkGenerator::worker_loop(size_t workerId) {
#ifdef TRACY_ENABLE
    char threadName[64];
    snprintf(threadName, sizeof(threadName), "PlanetChunkGenWorker %zu", workerId);
    tracy::SetThreadName(threadName);
#endif

    std::vector<ChunkGenInput> batch;
    std::vector<ChunkGenOutput> results;
    batch.reserve(GENERATION_BATCH_SIZE);
    results.reserve(GENERATION_BATCH_SIZE);

    PlanetWorldGenerator generator = PlanetWorldGenerator();

    while (true) {
        batch.clear();

        m_generationSemaphore.acquire();

        if (m_stopGeneration && [&] {
                std::lock_guard lock(m_enqueueGenerationMutex);
                return m_generationQueue.empty();
            }()) {
            return;
        }

        {
            VOXEL_ZONE_N("PollJobs");
            std::lock_guard lock(m_enqueueGenerationMutex);
            while (batch.size() < GENERATION_BATCH_SIZE && !m_generationQueue.empty()) {
                std::pop_heap(m_generationQueue.begin(), m_generationQueue.end(),
                              [](const ChunkGenInput &a, const ChunkGenInput &b) {
                                  return a.priority > b.priority; // max-heap
                              });
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
                VoxelChunk chunk = {};
                bool hasContent = generator.generate_planet_chunk(chunk, input.coord, input.config);

                ChunkGenOutput output;
                output.chunkEntity = input.chunkEntity;
                output.coord = input.coord;
                output.chunk = std::move(chunk);
                output.success = true; // or false if generation failed
                output.empty = !hasContent; // or true if the generated chunk is empty
                results.push_back(std::move(output));
            }

            auto &workerResult = m_generationWorkerResults[workerId]; {
                std::lock_guard lock(workerResult->m_resultMutex);
                for (auto &r: results) {
                    workerResult->results.push_back(std::move(r));
                }
                workerResult->pendingCount.fetch_add(static_cast<int>(results.size()), std::memory_order_relaxed);
            }
        }
    }
}
