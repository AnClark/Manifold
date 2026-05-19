#pragma once

#include "AudioStream.hpp"
#include "base/Report.hpp"

#include <any>
#include <atomic>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <functional>

/**
 * @brief Per-file processing context.
 *
 * One NodeContext instance is created per input file and flows through every
 * node in the chain. It carries path metadata and an open-ended sideband map
 * for cross-node communication (loudness, peaks, etc.).
 *
 * NodeContext is intentionally value-typed and never shared across threads or
 * files, which keeps parallel processing safe.
 */
class NodeContext {
public:
    // --------------------------------------------------------------------------
    // Path metadata

    std::string sourcePath;       ///< Original input file
    std::string outputDir;        ///< Requested output directory
    std::string currentFilePath;  ///< Set by Sink/Atomic nodes; read by downstream

    /**
     * @brief Pre-resolved output filename stem (no extension, no directory).
     *
     * When non-empty, OutputSinkNode uses this as the output file stem instead
     * of deriving one from the source path.  Set by SingleFileProcessorWorker
     * when a FilenameTemplate is active.
     */
    std::string outputStem;

    /**
     * @brief How to handle an existing file at the computed output path.
     */
    enum class OutputConflictPolicy { Skip, Overwrite, AutoRename };
    OutputConflictPolicy outputConflictPolicy = OutputConflictPolicy::AutoRename;

    // --------------------------------------------------------------------------
    // Audio format metadata

    /** Original source file format. Set once by FileSourceNode::create(). Read-only after that. */
    AudioFormat sourceFormat;

    /**
     * @brief Current format at this point in the chain.
     *
     * Updated by ChainEngine after every create() and wrap() call.
     * Nodes that need the live channel count / sample rate (e.g. DcOffsetRemoveNode,
     * PrintInfoNode) should read this rather than sourceFormat.
     */
    AudioFormat currentFormat;

    // --------------------------------------------------------------------------
    // Sideband data (typed key-value store for cross-node communication)

    std::unordered_map<std::string, std::any> sideband;

    /** @brief Optional callback for publishing reports to the active FileRunRecord. */
    std::function<void(std::shared_ptr<Report>)> onReport;

    // --------------------------------------------------------------------------
    // Cancellation

    /// Set by ChainEngine from the run's cancel token before processing starts.
    /// Sink nodes poll this in their pull loop to support mid-file cancellation.
    const std::atomic<bool>* cancelToken = nullptr;

    /// Returns true if the run has been cancelled.
    bool isCancelled() const
    {
        return cancelToken && cancelToken->load(std::memory_order_relaxed);
    }

    template<typename T>
    void setSideband(const std::string& key, T value) {
        sideband[key] = std::move(value);
    }

    template<typename T>
    std::optional<T> getSideband(const std::string& key) const {
        auto it = sideband.find(key);
        if (it != sideband.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (...) {}
        }
        return std::nullopt;
    }
};
