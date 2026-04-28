#include "Main.hpp"

#include "imgui.h"

// ---------------------------------------------------------------------------
// Static list of available node types shown in the Actions panel.
// Add an entry here whenever a new node type is introduced.
// ---------------------------------------------------------------------------
namespace {
struct NodeTypeInfo {
    const char* name;
    const char* description;
};
constexpr NodeTypeInfo kAvailableNodes[] = {
    { "LoudnessNormalize",
      "Normalizes integrated loudness (LUFS-I) to a target level with True Peak ceiling protection." },
    { "TruePeakLimiter",
      "Attenuates the signal so that the 4x oversampled True Peak does not exceed the configured ceiling (dBTP)." },
    { "Resampler",
      "Converts the audio stream to a target sample rate using a high-quality linear-phase resampler." },
    { "LoudnessAnalyzer",
      "Transparent pass-through that measures EBU R128 integrated loudness and max sample peak." },
};
} // namespace

void ManifoldApp::UI_Actions()
{
    if (ImGui::Begin("Actions"))
    {
        ImGui::Text("Available node types:");
        for (const auto& info : kAvailableNodes)
        {
            ImGui::BulletText("%s", info.name);
            ImGui::Indent();
            ImGui::BulletText("%s", info.description);
            ImGui::Unindent();
        }
    }
    ImGui::End();
}