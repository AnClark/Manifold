#include "LoudnessNormalizeNode.hpp"
#include "processors/LoudnessNormalizeProcessor.hpp"
#include "pipeline/NodeRegistry.hpp"

#include <imgui.h>
#include <string>

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

void LoudnessNormalizeNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("target_lufs");
    if (it != params.end()) {
        try { targetLufs_ = std::stof(it->second); } catch (...) {}
    }
    it = params.find("tp_ceiling");
    if (it != params.end()) {
        try { tpCeiling_ = std::stof(it->second); } catch (...) {}
    }
}

std::unordered_map<std::string, std::string> LoudnessNormalizeNode::exportConfig()
{
    return {
        { "target_lufs", std::to_string(targetLufs_) },
        { "tp_ceiling",  std::to_string(tpCeiling_)  },
    };
}

void LoudnessNormalizeNode::configureProcessorFromNodeContext(IAudioProcessor& proc, NodeContext& ctx)
{
    proc.setParameterValue("target_lufs", targetLufs_);
    proc.setParameterValue("tp_ceiling",  tpCeiling_);

    if (auto v = ctx.getSideband<double>("loudness_lufs"))
        proc.setParameterValue("measured_lufs", static_cast<float>(*v));

    if (auto v = ctx.getSideband<double>("loudness_peak_dbfs"))
        proc.setParameterValue("measured_peak_dbfs", static_cast<float>(*v));
}

void LoudnessNormalizeNode::drawUI()
{
    const float labelWidth = ImGui::CalcTextSize("True Peak Ceiling").x + 20.0f;

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Target Loudness");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##target_lufs", &targetLufs_, -36.0f, -6.0f, "%.1f LUFS");

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("True Peak Ceiling");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##tp_ceiling", &tpCeiling_, -9.0f, 0.0f, "%.1f dBTP");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Measurement provided by EBUR128 analysis.");
}
