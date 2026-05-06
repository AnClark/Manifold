#pragma once

#include "Node.hpp"
#include "../base/SndFileInfo.hpp"

#include <filesystem>
#include <memory>
#include <vector>

/**
 * @brief Executes an ordered node chain against one or more audio files.
 *
 * The engine scans the node list and identifies two kinds of segments:
 *
 *  1. **Stream segment**: SourceNode → [StreamProcessorNode…] → StreamSinkNode
 *     The sink drives the pull loop; all state is created fresh per file.
 *
 *  2. **Atomic nodes**: nodes that implement AtomicNode are executed in order
 *     between (or after) stream segments.
 *
 * Type safety: validateChain() checks segment structure at construction time
 * and throws std::runtime_error if the chain is malformed.
 *
 * Thread safety: each processFile() call uses an independent NodeContext; the
 * engine itself holds no mutable per-file state, making it safe to call
 * processFile() from multiple threads on the same engine instance (as long as
 * the nodes themselves are reentrant — which they are, since all per-file
 * state lives inside AudioStream objects returned from wrap()/create()).
 */
class ChainEngine {
public:
    /** @brief Construct from a non-owning view of nodes (raw pointers). */
    explicit ChainEngine(std::vector<Node*> nodes);

    /** @brief Process a single file through the chain. Errors are caught and logged. */
    void processFile(const std::string& input,
                     const std::string& outputDir);

    /** @brief Overload: resolve path from SndFileInfo::fileName. */
    void processFile(const SndFileInfo& info,
                     const std::string& outputDir);

    /**
     * @brief Converts an owning unique_ptr chain to a non-owning raw-pointer view.
     *
     * Useful when constructing a ChainEngine from an existing
     * std::vector<std::unique_ptr<Node>> without transferring ownership.
     */
    static std::vector<Node*> toView(const std::vector<std::unique_ptr<Node>>& nodes);

private:
    std::vector<Node*> nodes_;

    /** @brief Validates chain structure; throws on malformed chains. */
    void validateChain() const;

    /** @brief Core per-file execution (propagates exceptions). */
    void executeFor(const std::string& input,
                    const std::string& outputDir);

    /** @brief Overload: run the node chain against a pre-built context (propagates exceptions). */
    void executeFor(NodeContext& ctx);
};
