#pragma once

#include "DSPNode.hpp"

/**
 * @brief DSP node that limits True Peak (dBTP) with 4× oversampled detection.
 *
 * Wraps TruePeakLimiterProcessor. Before each file starts streaming,
 * configureProcessor() reads the per-file True Peak measurement written to
 * NodeContext::sideband by an upstream node (e.g. a future TruePeakAnalyzerNode):
 *
 *   "true_peak_dbtp" (double) → fed to "measured_true_peak_dbtp"
 *
 * If the sideband key is absent, the processor retains whatever value was set
 * via Node::init() (static configuration), which defaults to 0 dBTP.
 * Static parameter tp_ceiling can be set via Node::init().
 */
class TruePeakLimiterNode : public DSPNode {
public:
    TruePeakLimiterNode();

    std::string name() const override { return "TruePeakLimiter"; }

protected:
    void configureProcessor(IAudioProcessor& proc, NodeContext& ctx) override;
};
