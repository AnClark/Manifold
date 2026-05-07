#pragma once

#include "../base_nodes/DSPNode.hpp"

/**
 * @brief DSP node that removes per-channel DC offset from the audio signal.
 *
 * Wraps DcOffsetRemoveProcessor.  Before each file starts streaming,
 * configureProcessor() reads per-channel DC offset measurements written to
 * NodeContext::sideband by ChainEngine (originally from DcOffsetWorker):
 *
 *   "dc_offsets" (std::vector<double>) → converted to float and injected via
 *                                        DcOffsetRemoveProcessor::setOffsets()
 *
 * If the sideband key is absent (DcOffsetWorker did not run), the processor
 * is a transparent pass-through — no samples are modified.
 */
class DcOffsetRemoveNode : public DSPNode {
public:
    DcOffsetRemoveNode();

    std::string name() const override { return "DC Offset Remove"; }

    void drawUI() override;
    void getUiSize(float& width, float& height) override
    {
        width  = 0.0f;
        height = 100.0f + 8.0f;
    }

protected:
    void configureProcessorFromNodeContext(IAudioProcessor& proc, NodeContext& ctx) override;
};
