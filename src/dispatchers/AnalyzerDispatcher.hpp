#pragma once

#include "workers/EBUR128Worker.hpp"
#include "workers/DcOffsetWorker.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

/**
 * @brief Dispatches audio file analysis tasks across parallel worker pools.
 *
 * AnalyzerDispatcher owns two independently-sized pools: one for EBUR128Worker
 * and one for DcOffsetWorker. Incoming files are distributed across each pool
 * using a shared round-robin counter, applied modulo the respective pool size,
 * so files are spread evenly regardless of whether the two pool sizes differ.
 *
 * @note SndFileWorker is intentionally excluded from this dispatcher because
 *       header-only parsing (sf_open + metadata read) is extremely fast and
 *       does not benefit from parallelism.
 *
 * @warning The dispatcher does not lock the application's file list mutex
 *          internally. The caller is responsible for holding that lock while
 *          iterating or modifying the list (e.g. inside _ingestPaths()).
 *          Passing the mutex into addFiles() would cause a deadlock if the
 *          caller already holds it.
 */
class AnalyzerDispatcher
{
public:
    /**
     * @brief Constructs the dispatcher and starts all worker threads.
     *
     * @param ebuR128Threads  Number of EBUR128Worker instances to create.
     *                        Each runs on its own thread.
     * @param dcOffsetThreads Number of DcOffsetWorker instances to create.
     *                        Each runs on its own thread.
     */
    AnalyzerDispatcher(uint32_t ebuR128Threads, uint32_t dcOffsetThreads);

    /**
     * @brief Stops all workers and joins their threads.
     *
     * Calls requestCancelProcessing() before destruction so that any in-flight
     * analysis is abandoned promptly rather than drained to completion.
     */
    ~AnalyzerDispatcher();

    /**
     * @brief Replaces the worker pools with new instances at the requested sizes.
     *
     * Cancels any in-flight analysis, destroys the existing worker threads (blocking
     * until each thread joins), then creates fresh pools with the requested sizes.
     *
     * @param newEbuR128Threads  New number of EBUR128Worker instances.
     * @param newDcOffsetThreads New number of DcOffsetWorker instances.
     *
     * @warning Files that are currently queued in the old workers will be lost.
     *          Call this method only when no analysis is in progress, or when
     *          discarding in-flight work is acceptable.
     * @warning Not thread-safe with respect to concurrent addFile() calls.
     *          Ensure no other thread is submitting files while this runs.
     */
    void changeParallelThreads(uint32_t newEbuR128Threads, uint32_t newDcOffsetThreads);

    /**
     * @brief Submits a single file for parallel analysis.
     *
     * Increments a shared atomic counter and selects the target worker instance
     * for each pool independently (counter % ebuR128Threads_ for EBUR128,
     * counter % dcOffsetThreads_ for DC offset), so the two pools are balanced
     * separately even when their sizes differ. The atomic increment makes this
     * method safe to call from multiple threads simultaneously.
     *
     * @param file Shared pointer to the SndFileInfo to be analysed. The pointer
     *             must remain valid until the worker has finished processing it
     *             (guaranteed as long as the file is kept in the application's
     *             sndFileList).
     *
     * @note This is the preferred interface for incremental ingestion.
     *       Call it once per newly added file inside the ingestion loop so
     *       that each file is submitted exactly once.
     */
    void addFile(std::shared_ptr<SndFileInfo> file);

    /**
     * @brief Submits every file in @p fileListFromApp for analysis.
     *
     * Iterates the entire list and calls addFile() for each element.
     *
     * @param fileListFromApp Reference to the application's complete file list.
     *
     * @warning This method re-submits **all** files in the list, including those
     *          that have already been analysed. Because each worker's processItem()
     *          resets the relevant status flags (isR128ParsedOK,
     *          isDcOffsetCalculatedOK) at the start of processing, calling this
     *          method on a list that already contains analysed files will cause
     *          those files to be re-analysed and their results to be overwritten.
     *          Use addFile() for incremental ingestion; reserve addFiles() only
     *          for intentional full re-analysis of the entire list.
     *
     * @note The caller must NOT hold the application's sndFileListMutex when
     *       calling this method if addFile() or any other code path could also
     *       try to acquire that mutex — doing so risks a deadlock.
     */
    void addFiles(SndFileList& fileListFromApp);

    /**
     * @brief Requests all worker instances to abandon their current and pending tasks.
     *
     * Sets the cancellation flag on every EBUR128Worker and DcOffsetWorker in
     * the pools. Already-started processItem() calls will notice the flag at
     * their next cancellation checkpoint and return early.
     */
    void requestCancelProcessing();

private:
    std::vector<std::unique_ptr<EBUR128Worker>>  ebuR128WorkerPool_;
    std::vector<std::unique_ptr<DcOffsetWorker>> dcOffsetWorkerPool_;

    uint32_t ebuR128Threads_, dcOffsetThreads_;

    /// Shared round-robin counter used by addFile(); applied modulo each pool's
    /// size independently, so both pools stay balanced even when their sizes differ.
    std::atomic<uint32_t> fileCounter_{0};
};
