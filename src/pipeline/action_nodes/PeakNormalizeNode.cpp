#include "PeakNormalizeNode.hpp"
#include "processors/PeakNormalizeProcessor.hpp"
#include "pipeline/NodeRegistry.hpp"

#include <imgui.h>
#include <string>

REGISTER_NODE(
    PeakNormalizeNode,
    "peak_normalize",
    "Peak Normalize",
    "Normalizes the sample peak to a target dBFS level. Measurement is provided by EBUR128 analysis.",
    "Dynamics",
    NodeRole::StreamProcessor);

PeakNormalizeNode::PeakNormalizeNode()
    : DSPNode(DSPNode::makeProcessor<PeakNormalizeProcessor>(), "Peak Normalize")
{}

void PeakNormalizeNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("target_dbfs");
    if (it != params.end()) {
        try { targetDbfs_ = std::stof(it->second); } catch (...) {}
    }
}

std::unordered_map<std::string, std::string> PeakNormalizeNode::exportConfig()
{
    return {
        { "target_dbfs", std::to_string(targetDbfs_) },
    };
}

void PeakNormalizeNode::configureProcessorFromNodeContext(IAudioProcessor& proc, NodeContext& ctx)
{
    // Push the current UI value so init() and slider changes both take effect
    proc.setParameterValue("target_dbfs", targetDbfs_);

    // Inject per-file measured peak from sideband (written by EBUR128Worker or LoudnessAnalyzerNode)
    if (auto v = ctx.getSideband<double>("loudness_peak_dbfs"))
        proc.setParameterValue("measured_peak_dbfs", static_cast<float>(*v));
}

void PeakNormalizeNode::drawUI()
{
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Target Peak");
    ImGui::SameLine(0, 10.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##target_dbfs", &targetDbfs_, -36.0f, 0.0f, "%.1f dBFS");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Measurement provided by EBUR128 analysis.");
}
