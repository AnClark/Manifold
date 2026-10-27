#pragma once

#include "workers/SingleFileProcessorWorker.hpp"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/**
 * @brief Dispatches audio file batch-processing tasks across a parallel worker pool.
 *
 * ProcessingDispatcher owns a pool of SingleFileProcessorWorker instances, each
 * running on its own thread. Incoming files are distributed across the pool using
 * a shared round-robin counter, applied modulo the pool size, so files are spread
 * evenly and several files are processed simultaneously.
 *
 * Unlike AnalyzerDispatcher (which owns two independently-sized pools, one per
 * analyzer type), batch processing has only one worker type, so this dispatcher
 * manages a single pool.
 *
 * All processing configuration (node chain, output directory, output format,
 * sink mode, conflict policy) is kept in the dispatcher and broadcast to every
 * worker in the pool. It is re-applied automatically whenever
 * changeParallelThreads() rebuilds the pool.
 *
 * @note The shared node chain is safe to execute from several workers at once:
 *       ChainEngine holds no mutable per-file state, and every node keeps its
 *       per-file state inside the AudioStream objects created for each file
 *       (see ChainEngine's thread-safety note). Only node configuration is
 *       shared, and processing merely reads it.
 *
 * @warning The dispatcher does not lock the application's file list mutex
 *          internally. The caller is responsible for holding that lock while
 *          iterating or modifying the list (e.g. inside _ingestPaths()).
 *          Passing the mutex into addFiles() would cause a deadlock if the
 *          caller already holds it.
 *
 * @warning Output filename conflict resolution (Skip / AutoRename in
 *          OutputSinkNode) is not atomic across workers. When two files resolve
 *          to the same output stem, parallel workers may both observe "file does
 *          not exist" and race. The UI thread pre-resolves a unique stem per file
 *          from the FilenameTemplate (which includes a file counter), so this
 *          does not matter in practice; only templates without a counter token
 *          can collide.
 */
class ProcessingDispatcher
{
public:
    /**
     * @brief Constructs the dispatcher and starts all worker threads.
     *
     * @param processingThreads Number of SingleFileProcessorWorker instances to
     *                          create. Each runs on its own thread. Must be >= 1.
     */
    ProcessingDispatcher(uint32_t processingThreads);

    /**
     * @brief Stops all workers and joins their threads.
     *
     * Calls requestCancelProcessing() before destruction so that queued files
     * are skipped rather than processed. A file that is already being processed
     * runs to completion (or is interrupted via its run's cancel token) before
     * the worker thread joins.
     */
    ~ProcessingDispatcher();

    /**
     * @brief Replaces the worker pool with new instances at the requested size.
     *
     * Cancels any in-flight processing, destroys the existing worker threads (blocking
     * until each thread joins), then creates a fresh pool with the requested size.
     * The stored configuration (node chain, output settings, …) is re-applied to
     * every new worker.
     *
     * @param newProcessingThreads New number of SingleFileProcessorWorker instances.
     *
     * @warning Files that are currently queued in the old workers will be lost.
     *          Call this method only when no processing is in progress, or when
     *          discarding in-flight work is acceptable.
     * @warning Not thread-safe with respect to concurrent addFile() calls.
     *          Ensure no other thread is submitting files while this runs.
     */
    void changeParallelThreads(uint32_t newProcessingThreads);

    /**
     * @brief Points every worker at the application's node chain.
     *
     * Called once (UI thread) during start-up. The references are stored so that
     * workers created later by changeParallelThreads() receive them too.
     *
     * @param nodeChain Reference to the application's node chain.
     * @param mutex     Mutex protecting @p nodeChain against concurrent access.
     */
    void setNodeChain(std::vector<std::shared_ptr<Node>>& nodeChain, std::mutex& mutex);

    /**
     * @brief Sets the output directory on every worker.
     *
     * Called once per run (UI thread) before the first addFile of that run.
     */
    void setOutputDir(std::string outputDir);

    /**
     * @brief Sets the output container format / subtype override on every worker.
     */
    void setOutputFormat(ContainerFormat format, SubtypeOverride subtype);

    /**
     * @brief Sets what happens at the end of the audio pipeline on every worker.
     */
    void setSinkMode(SinkMode mode);

    /**
     * @brief Set the conflict policy used when an output file already exists.
     *
     * Called once per run (UI thread) before the first addFile of that run.
     * The per-file output stem is pre-resolved by the UI thread and stored in
     * FileRunRecord::resolvedOutputStem; the workers read it directly.
     */
    void setConflictPolicy(FilenameTemplate::ConflictPolicy policy);

    /**
     * @brief Submits a single file for parallel processing (standalone, no run record).
     *
     * Increments a shared atomic counter and selects the target worker instance
     * (counter % processingThreads_), so files are balanced across the pool.
     * The atomic increment makes this method safe to call from multiple threads
     * simultaneously.
     *
     * @param file Shared pointer to the SndFileInfo to be processed. The pointer
     *             must remain valid until the worker has finished processing it
     *             (guaranteed as long as the file is kept in the application's
     *             sndFileList).
     *
     * @note This is the preferred interface for incremental submission without a
     *       ProcessingRun. The file is processed with no run record (legacy /
     *       standalone semantics of SingleFileProcessorWorker::addFile()).
     */
    void addFile(std::shared_ptr<SndFileInfo> file);

    /**
     * @brief Submits a single file as part of a ProcessingRun.
     *
     * Dispatches exactly like addFile(), but pairs the file with its
     * FileRunRecord inside the target worker's queue. The record will be driven
     * through its lifecycle by that worker's thread.
     *
     * @param file   Shared pointer to the SndFileInfo to be processed.
     * @param record Per-file result record owned by the active ProcessingRun.
     */
    void addFile(std::shared_ptr<SndFileInfo> file, std::shared_ptr<FileRunRecord> record);

    /**
     * @brief Submits every file in @p fileListFromApp for processing (standalone).
     *
     * Iterates the entire list and calls addFile() for each element (no run
     * records — see the standalone addFile() overload).
     *
     * @param fileListFromApp Reference to the application's complete file list.
     *
     * @note The caller must NOT hold the application's sndFileListMutex when
     *       calling this method if addFile() or any other code path could also
     *       try to acquire that mutex — doing so risks a deadlock.
     */
    void addFiles(SndFileList& fileListFromApp);

    /**
     * @brief Requests all worker instances to abandon their current and pending tasks.
     *
     * Sets the cancellation flag on every SingleFileProcessorWorker in the pool.
     * Files still sitting in a worker's queue are skipped (IWorker checks the
     * flag before each processItem()). A file that is already being processed
     * runs to completion unless its ProcessingRun's cancel token fires mid-stream.
     */
    void requestCancelProcessing();

    /**
     * @brief Clears the cancellation flag on all worker instances.
     *
     * Call this after requestCancelProcessing() to allow workers to accept and
     * process new tasks again (e.g. before re-submitting files via addFiles()).
     */
    void resumeProcessing();

private:
    /// Pushes the stored configuration into one worker. Caller must hold configMutex_.
    void applyConfigTo(SingleFileProcessorWorker& worker);

    std::vector<std::unique_ptr<SingleFileProcessorWorker>> workerPool_;

    uint32_t threads_;

    /// Shared round-robin counter used by addFile(); applied modulo the pool size.
    std::atomic<uint32_t> fileCounter_{0};

    // Stored processing configuration — broadcast to every worker on set, and
    // re-applied to workers created by changeParallelThreads().
    std::mutex configMutex_;
    std::vector<std::shared_ptr<Node>>* nodeChain_      = nullptr;
    std::mutex*                          nodeChainMutex_ = nullptr;
    std::string outputDir_;
    ContainerFormat    outputFormat_  = ContainerFormat::Wav;
    SubtypeOverride    outputSubType_ = SubtypeOverride::Pcm16;
    SinkMode           sinkMode_      = SinkMode::WriteFile;
    FilenameTemplate::ConflictPolicy conflictPolicy_ = FilenameTemplate::ConflictPolicy::AutoRename;
};