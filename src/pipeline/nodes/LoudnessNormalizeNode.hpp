#pragma once

#include "DSPNode.hpp"

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
 * Static parameters (target_lufs, tp_ceiling) can be set via Node::init().
 */
class LoudnessNormalizeNode : public DSPNode {
public:
    LoudnessNormalizeNode();

    std::string name() const override { return "LoudnessNormalize"; }

protected:
    void configureProcessor(IAudioProcessor& proc, NodeContext& ctx) override;
};
