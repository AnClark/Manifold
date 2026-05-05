#include "DcOffsetRemoveNode.hpp"
#include "processors/DcOffsetRemoveProcessor.hpp"
#include "pipeline/NodeRegistry.hpp"

#include <imgui.h>

#include <vector>

REGISTER_NODE(
    DcOffsetRemoveNode,
    "dc_offset_remove",
    "DC Offset Remove",
    "Subtracts the measured per-channel DC offset from the signal.",
    "Repair",
    NodeRole::StreamProcessor);

DcOffsetRemoveNode::DcOffsetRemoveNode()
    : DSPNode(DSPNode::makeProcessor<DcOffsetRemoveProcessor>(), "DC Offset Remove")
{}

void DcOffsetRemoveNode::configureProcessorFromNodeContext(IAudioProcessor& proc, NodeContext& ctx)
{
    auto currentDcOffset = ctx.getSideband<std::vector<double>>("dc_offsets");
    if (!currentDcOffset.has_value())
        return;

    std::vector<float> floatOffsets;
    floatOffsets.reserve(currentDcOffset->size());
    for (double d : *currentDcOffset)
        floatOffsets.push_back(static_cast<float>(d));

    dynamic_cast<DcOffsetRemoveProcessor&>(proc).setOffsets(floatOffsets);
}

void DcOffsetRemoveNode::drawUI()
{
    ImGui::Spacing();
    ImGui::TextWrapped("This Node subtracts the measured per-channel DC bias from the audio signal.");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("DC offset is measured automatically when files are loaded.");
}
