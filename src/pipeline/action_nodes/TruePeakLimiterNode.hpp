#pragma once

#include "../base_nodes/DSPNode.hpp"

/**
 * @brief DSP node that limits True Peak (dBTP) with 4× oversampled detection.
 *
 * Unlike other DSPNode subclasses, this node overrides wrap() entirely to
 * implement a self-contained two-pass approach:
 *
 *   1. Drain the entire upstream stream into an in-memory buffer.
 *   2. Measure True Peak via TruePeakLimiterProcessor::measureTruePeakDbtp()
 *      (4× oversampled, r8brain CDSPResampler24).
 *   3. Compute the required attenuation and replay the buffer with that gain.
 *
 * No external sideband injection is needed; the node is fully self-contained.
 *
 * Parameters (set via Node::init()):
 *   "tp_ceiling"  — True Peak ceiling in dBTP (default: -1.0)
 */
class TruePeakLimiterNode : public DSPNode {
public:
    TruePeakLimiterNode();

    NODE_ID_AND_NAME_GETTER_DEFINITION;

    void init(const std::unordered_map<std::string, std::string>& params) override;

    std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) override;

    void drawUI() override;
    void getUiSize(float& width, float& height) override
    {
        width  = 0.0f;
        height = 100.0f + 8.0f;
    }

private:
    float tpCeiling_ = -1.0f;  ///< dBTP ceiling; matches TruePeakLimiterProcessor default
};
