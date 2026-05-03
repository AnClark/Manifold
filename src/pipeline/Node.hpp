#pragma once

#include "AudioStream.hpp"
#include "NodeContext.hpp"

#include <memory>
#include <string>
#include <unordered_map>

// --------------------------------------------------------------------------
// Port type enum
// --------------------------------------------------------------------------

enum class PortType {
    None,
    AudioStream,  ///< Pull-based float sample stream
    FilePath,     ///< Filesystem path
    Metadata,     ///< Key-value side data
    Trigger       ///< Control pulse (fire-and-forget)
};

// --------------------------------------------------------------------------
// Node base class
// --------------------------------------------------------------------------

/**
 * @brief Base class for every node in the processing chain.
 *
 * Concrete nodes should also inherit from exactly one of the four role
 * interfaces below (SourceNode, StreamProcessorNode, StreamSinkNode,
 * AtomicNode). ChainEngine uses dynamic_cast to determine each node's role
 * at runtime.
 */
class Node {
public:
    virtual ~Node() = default;

    virtual std::string name() const = 0;

    /**
     * @brief Tell others what the type of the node is. Useful for determining Node type
     * on some situations (e.g. Action view), without RTTI.
     *
     * This is optional. Not all types of node needs this.
     */
    virtual std::string nodeHint() const { return "Unspecified"; }

    /**
     * @brief Configure the node from a string-string parameter map.
     *
     * Called once during chain construction. Throws std::invalid_argument on
     * bad parameters.
     */
    virtual void init(const std::unordered_map<std::string, std::string>& params) = 0;

    virtual PortType primaryInput()  const = 0;
    virtual PortType primaryOutput() const = 0;

    /**
     * @brief Draw the node's UI inside the Action Editor panel. Optional; only needed for nodes with
     * configurable parameters.
     *
     * Called by the Action Editor when the node is selected. The node can use ImGui calls to draw its UI,
     * and should use getUiSize() to report its desired size.
     */
    virtual void drawUI() {}

    /**
     * @brief Get the desired size of the node's UI panel in the Action Editor. Optional; only needed for nodes with
     * configurable parameters.
     *
     * @note The default size is for debug purposes, and should be overridden by nodes with actual UI.
     */
    virtual void getUiSize(float& width, float& height)
    {
        width = 0.0f;
        height = 100.0f;
    }
};

// --------------------------------------------------------------------------
// Role interfaces
// --------------------------------------------------------------------------

/**
 * @brief Audio source: creates a fresh AudioStream for each file.
 */
class SourceNode {
public:
    virtual ~SourceNode() = default;
    virtual std::unique_ptr<AudioStream> create(NodeContext& ctx) = 0;
};

/**
 * @brief Audio stream processor: wraps an upstream stream (decorator pattern).
 *
 * Each call to wrap() returns a new stream object whose read() pulls from the
 * provided upstream and applies the transformation. The processor itself is
 * stateless across files; all per-file state lives inside the returned stream.
 */
class StreamProcessorNode {
public:
    virtual ~StreamProcessorNode() = default;
    virtual std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) = 0;
};

/**
 * @brief Audio sink: drives the pull loop and writes to a destination.
 *
 * consume() must contain an internal `while (stream->read(...))` loop.
 * After the loop it should write ctx.currentFilePath if it produced a file.
 * The stream unique_ptr is destroyed at the end of consume(), which triggers
 * any stream-level finalisation (e.g. loudness write-back).
 */
class StreamSinkNode {
public:
    virtual ~StreamSinkNode() = default;
    virtual void consume(std::unique_ptr<AudioStream> stream, NodeContext& ctx) = 0;
};

/**
 * @brief Atomic node: non-streaming operation (file copy, upload, print, …).
 *
 * execute() is called after the preceding stream segment has finished.
 * It can read and write ctx freely.
 */
class AtomicNode {
public:
    virtual ~AtomicNode() = default;
    virtual void execute(NodeContext& ctx) = 0;
};
