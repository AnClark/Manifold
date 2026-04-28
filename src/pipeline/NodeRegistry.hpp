#pragma once

#include "Node.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// --------------------------------------------------------------------------
// NodeRole — topological role of a registerable node in the chain
// --------------------------------------------------------------------------

/**
 * @brief The topological role of a user-facing node.
 *
 * Only user-configurable middle-segment nodes are registered.
 * Structural nodes (FileSourceNode, OutputSinkNode, PrintInfoNode) are
 * intentionally excluded from the registry.
 */
enum class NodeRole {
    StreamProcessor,  ///< Wraps an upstream AudioStream (StreamProcessorNode)
    Atomic,           ///< Executes as a side-effect step between segments (AtomicNode)
};

// --------------------------------------------------------------------------
// NodeDescriptor — full metadata for one registered node type
// --------------------------------------------------------------------------

struct NodeDescriptor {
    std::string id;           ///< Unique factory key, e.g. "loudness_normalize"
    std::string displayName;  ///< Human-readable name shown in UI
    std::string description;  ///< Short explanation of what the node does
    std::string category;     ///< UI grouping label, e.g. "Dynamics", "Analysis"
    NodeRole    role;         ///< Topological role (StreamProcessor or Atomic)
};

// --------------------------------------------------------------------------
// NodeRegistry — singleton registry for user-facing node types
// --------------------------------------------------------------------------

/**
 * @brief Singleton registry for user-facing Node types.
 *
 * Stores metadata (NodeDescriptor) and a factory for each registered type.
 * Nodes self-register at startup via AutoRegisterNode / REGISTER_NODE.
 *
 * Only user-configurable nodes (DSP processors, analyzers) are registered.
 * Structural nodes (source, sink, printer) are NOT registered here.
 *
 * @note Registration happens before main() runs; no locking is required.
 */
class NodeRegistry {
public:
    using Factory = std::function<std::unique_ptr<Node>()>;

    /** @brief Returns the global singleton instance (Meyers Singleton). */
    static NodeRegistry& getInstance();

    /**
     * @brief Registers a node type.
     *
     * Prefer the REGISTER_NODE macro over calling this directly.
     * Registering the same id twice overwrites the previous entry.
     */
    void reg(NodeDescriptor descriptor, Factory factory);

    /**
     * @brief Creates a fresh instance of the node identified by @p id.
     * @throws std::runtime_error if @p id is not registered.
     */
    std::unique_ptr<Node> create(std::string_view id) const;

    /** @brief Returns all descriptors in registration order. */
    const std::vector<NodeDescriptor>& listAll() const;

    /**
     * @brief Returns pointers to descriptors for nodes in @p category.
     *
     * Pointers remain valid as long as the registry is not modified.
     */
    std::vector<const NodeDescriptor*> listByCategory(std::string_view category) const;

    /**
     * @brief Returns all distinct category strings in first-seen order.
     *
     * Useful for iterating categories in the order nodes were registered,
     * which reflects the order they appear in the UI.
     */
    std::vector<std::string> listCategories() const;

private:
    std::vector<NodeDescriptor>              descriptors_;
    std::unordered_map<std::string, Factory> factories_;
};

// --------------------------------------------------------------------------
// AutoRegisterNode / REGISTER_NODE macro
// --------------------------------------------------------------------------

/**
 * @brief Compile-time self-registration helper.
 *
 * Place a static instance of this in a node's .cpp file (via REGISTER_NODE)
 * to register it before main() runs.
 *
 * @tparam T  Concrete Node subclass; must be default-constructible.
 */
template<typename T>
struct AutoRegisterNode {
    explicit AutoRegisterNode(NodeDescriptor descriptor) {
        NodeRegistry::getInstance().reg(
            std::move(descriptor),
            [] { return std::make_unique<T>(); });
    }
};

/**
 * @brief Registers a node type in the NodeRegistry.
 *
 * Place this macro at file scope in the node's .cpp file.
 *
 * @param ClassType   Concrete Node subclass (must be default-constructible).
 * @param Id          String ID used as factory key, e.g. "loudness_normalize".
 * @param DisplayName Human-readable UI label.
 * @param Description Short description of what the node does.
 * @param Category    UI grouping string, e.g. "Dynamics", "Analysis".
 * @param Role        NodeRole::StreamProcessor or NodeRole::Atomic.
 */
#define REGISTER_NODE(ClassType, Id, DisplayName, Description, Category, Role)   \
    static AutoRegisterNode<ClassType> autoRegisterNode##ClassType(              \
        NodeDescriptor{Id, DisplayName, Description, Category, Role});
