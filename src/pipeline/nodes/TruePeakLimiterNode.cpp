#include "TruePeakLimiterNode.hpp"
#include "processors/TruePeakLimiterProcessor.hpp"

TruePeakLimiterNode::TruePeakLimiterNode()
    : DSPNode([] { return std::make_unique<TruePeakLimiterProcessor>(); },
              "TruePeakLimiter")
{}

void TruePeakLimiterNode::configureProcessor(IAudioProcessor& proc, NodeContext& ctx)
{
    if (auto v = ctx.getSideband<double>("true_peak_dbtp"))
        proc.setParameterValue("measured_true_peak_dbtp", static_cast<float>(*v));
}
