#pragma once

#include "../Node.hpp"

#include <string>
#include <unordered_map>

/**
 * @brief Stream processor that removes leading and trailing silence from audio.
 *
 * The node buffers the entire upstream into memory during wrap(), scans for the
 * first and last non-silent frames, applies a configurable padding around those
 * points, then replays only the retained region.
 *
 * A frame is considered silent when the absolute value of every sample it
 * contains is below the linear equivalent of @p threshold_dbfs.
 *
 * If the entire stream is silent the node emits a warning and returns the
 * audio unchanged (no frames are dropped).  An empty upstream (zero frames)
 * is treated as an error and causes wrap() to throw.
 *
 * ### Parameters (set via Node::init() or adjusted in drawUI())
 * | Key              | Default | Range      | Description                                      |
 * |------------------|---------|------------|--------------------------------------------------|
 * | `threshold_dbfs` | -60.0   | -96 ..   0 | Silence threshold (dBFS)                         |
 * | `padding_ms`     |  10.0   |   0 .. 500 | Margin kept around detected content (ms)         |
 */
class TrimSilenceNode : public Node, public StreamProcessorNode {
public:
    TrimSilenceNode() = default;

    NODE_ID_AND_NAME_GETTER_DEFINITION;

    void init(const std::unordered_map<std::string, std::string>& params) override;
    std::unordered_map<std::string, std::string> exportConfig() override;

    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::AudioStream; }

    std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) override;

    void drawUI() override;
    void getUiSize(float& width, float& height) override
    {
        width  = 0.0f;
        height = 150.0f;
    }

private:
    float thresholdDbfs_ = -60.0f;  ///< dBFS silence threshold
    float paddingMs_     =  10.0f;  ///< Padding around detected content (ms)
};
