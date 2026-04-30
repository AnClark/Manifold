#include "LoudnessNormalizeNode.hpp"
#include "processors/LoudnessNormalizeProcessor.hpp"
#include "pipeline/NodeRegistry.hpp"

REGISTER_NODE(
    LoudnessNormalizeNode,
    "loudness_normalize",
    "Loudness Normalize",
    "Normalizes integrated loudness (LUFS-I) to a target level with True Peak ceiling protection.",
    "Dynamics",
    NodeRole::StreamProcessor);

LoudnessNormalizeNode::LoudnessNormalizeNode()
    : DSPNode(DSPNode::makeProcessor<LoudnessNormalizeProcessor>(), "LoudnessNormalize")
{}

void LoudnessNormalizeNode::configureProcessorFromNodeContext(IAudioProcessor& proc, NodeContext& ctx)
{
    if (auto v = ctx.getSideband<double>("loudness_lufs"))
        proc.setParameterValue("measured_lufs", static_cast<float>(*v));

    if (auto v = ctx.getSideband<double>("loudness_peak_dbfs"))
        proc.setParameterValue("measured_peak_dbfs", static_cast<float>(*v));
}
