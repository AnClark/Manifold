#pragma once

#include "AudioStream.hpp"

#include <any>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>

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

    std::filesystem::path sourcePath;       ///< Original input file
    std::filesystem::path outputDir;        ///< Requested output directory
    std::filesystem::path currentFilePath;  ///< Set by Sink/Atomic nodes; read by downstream

    // --------------------------------------------------------------------------
    // Source audio format (populated by FileSourceNode::create)

    AudioFormat sourceFormat;

    // --------------------------------------------------------------------------
    // Sideband data (typed key-value store for cross-node communication)

    std::unordered_map<std::string, std::any> sideband;

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
