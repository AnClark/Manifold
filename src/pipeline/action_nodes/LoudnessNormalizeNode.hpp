#pragma once

#include "../base_nodes/DSPNode.hpp"

/**
 * @brief DSP node that applies integrated loudness (LUFS-I) normalisation.
 *
 * Wraps LoudnessNormalizeProcessor. Before each file starts streaming,
 * configureProcessor() reads the per-file LUFS-I and sample-peak measurements
 * written to NodeContext::sideband by an upstream LoudnessAnalyzerNode:
 *
 *   "loudness_lufs"      (double) → fed to "measured_lufs"
 *   "loudness_peak_dbfs" (double) → fed to "measured_peak_dbfs"
 *
 * Static parameters (target_lufs, tp_ceiling) can be set via Node::init() or
 * adjusted interactively through drawUI().
 */
class LoudnessNormalizeNode : public DSPNode {
public:
    LoudnessNormalizeNode();

    NODE_ID_AND_NAME_GETTER_DEFINITION;

    void init(const std::unordered_map<std::string, std::string>& params) override;

    void drawUI() override;
    void getUiSize(float& width, float& height) override
    {
        width  = 0.0f;
        height = 148.0f;
    }

protected:
    void configureProcessorFromNodeContext(IAudioProcessor& proc, NodeContext& ctx) override;

private:
    float targetLufs_ = -16.0f;
    float tpCeiling_  =  -1.0f;
};
