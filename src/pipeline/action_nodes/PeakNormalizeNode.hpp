#pragma once

#include "../base_nodes/DSPNode.hpp"

/**
 * @brief DSP node that normalizes the sample peak to a target dBFS level.
 *
 * Wraps PeakNormalizeProcessor. Before each file starts streaming,
 * configureProcessor() injects:
 *
 *   target_dbfs        ← targetDbfs_ (set in drawUI via slider, or via init())
 *   measured_peak_dbfs ← sideband "loudness_peak_dbfs" (double), populated by
 *                        EBUR128Worker / LoudnessAnalyzerNode
 *
 * If the sideband key is absent, measured_peak_dbfs retains its default of
 * -1.0, matching target_dbfs default — resulting in 0 dB gain (pass-through).
 */
class PeakNormalizeNode : public DSPNode {
public:
    PeakNormalizeNode();

    std::string name() const override { return "Peak Normalize"; }

    void init(const std::unordered_map<std::string, std::string>& params) override;

    void drawUI() override;
    void getUiSize(float& width, float& height) override
    {
        width  = 0.0f;
        height = 100.0f;
    }

protected:
    void configureProcessorFromNodeContext(IAudioProcessor& proc, NodeContext& ctx) override;

private:
    float targetDbfs_ = -1.0f;
};
