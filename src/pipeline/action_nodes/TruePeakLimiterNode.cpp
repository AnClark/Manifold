#include "TruePeakLimiterNode.hpp"
#include "processors/TruePeakLimiterProcessor.hpp"
#include "pipeline/NodeRegistry.hpp"

REGISTER_NODE(
    TruePeakLimiterNode,
    "true_peak_limiter",
    "True Peak Limiter",
    "Attenuates the signal so that the 4x oversampled True Peak does not exceed the configured ceiling (dBTP).",
    "Dynamics",
    NodeRole::StreamProcessor);

TruePeakLimiterNode::TruePeakLimiterNode()
    : DSPNode(DSPNode::factoryFor<TruePeakLimiterProcessor>(), "TruePeakLimiter")
{}

void TruePeakLimiterNode::configureProcessor(IAudioProcessor& proc, NodeContext& ctx)
{
    if (auto v = ctx.getSideband<double>("true_peak_dbtp"))
        proc.setParameterValue("measured_true_peak_dbtp", static_cast<float>(*v));
}
