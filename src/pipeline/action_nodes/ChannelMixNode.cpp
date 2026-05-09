#include "ChannelMixNode.hpp"
#include "pipeline/NodeRegistry.hpp"
#include "utils/LogManager.hpp"

#include <imgui.h>

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

REGISTER_NODE(
    ChannelMixNode,
    "channel_mix",
    "Channel Mix",
    "Remaps audio channels according to a selected mix mode (e.g. Stereo→Mono, Surround→Stereo).",
    "Format",
    NodeRole::StreamProcessor);

// ============================================================================
// ChannelMixStream
// ============================================================================

namespace {

// ITU-R BS.775 downmix coefficients
static constexpr float kCenter   = 0.7071f;  // -3 dB  (1/sqrt(2)) — center channel
static constexpr float kSurround = 0.7071f;  // -3 dB  — side surround channels
static constexpr float kRear     = 0.5000f;  // -6 dB  — rear channels (7.1 only)

/**
 * @brief Returns the number of output channels for a given mode and input count.
 *
 * Centralised here so ChannelMixStream and wrap() stay in sync without
 * duplicating the switch table.
 */
int outputChannels(ChannelMixNode::MixMode mode, int /*inCh*/)
{
    switch (mode) {
        case ChannelMixNode::MixMode::StereoToMono:     return 1;
        case ChannelMixNode::MixMode::MonoToStereo:     return 2;
        case ChannelMixNode::MixMode::SurroundToStereo: return 2;
        case ChannelMixNode::MixMode::ToMono:           return 1;
    }
    return 1;
}

/**
 * @brief Validates that the upstream channel count is compatible with the mode.
 * @throws std::runtime_error on mismatch.
 */
void validateChannels(ChannelMixNode::MixMode mode, int inCh)
{
    switch (mode) {
        case ChannelMixNode::MixMode::StereoToMono:
            if (inCh != 2)
                throw std::runtime_error(
                    "[ChannelMix] Stereo\xe2\x86\x92Mono requires exactly 2 input channels, got " +
                    std::to_string(inCh));
            break;
        case ChannelMixNode::MixMode::MonoToStereo:
            if (inCh != 1)
                throw std::runtime_error(
                    "[ChannelMix] Mono\xe2\x86\x92Stereo requires exactly 1 input channel, got " +
                    std::to_string(inCh));
            break;
        case ChannelMixNode::MixMode::SurroundToStereo:
            if (inCh != 6 && inCh != 8)
                throw std::runtime_error(
                    "[ChannelMix] Surround\xe2\x86\x92Stereo requires 6-ch (5.1) or 8-ch (7.1) input, got " +
                    std::to_string(inCh));
            break;
        case ChannelMixNode::MixMode::ToMono:
            // Accepts any channel count — no validation needed.
            break;
    }
}

// ----------------------------------------------------------------------------

class ChannelMixStream : public AudioStream {
public:
    ChannelMixStream(std::unique_ptr<AudioStream> upstream,
                     ChannelMixNode::MixMode mode,
                     int inChannels)
        : upstream_(std::move(upstream))
        , mode_(mode)
        , inChannels_(inChannels)
    {
        outFmt_          = upstream_->format();
        outFmt_.channels = outputChannels(mode, inChannels);
    }

    size_t read(float* buffer, size_t frames) override
    {
        // Pull interleaved upstream frames into per-call scratch storage.
        scratch_.resize(frames * static_cast<size_t>(inChannels_));
        const size_t got = upstream_->read(scratch_.data(), frames);
        if (got == 0) return 0;

        const int outCh = outFmt_.channels;
        std::memset(buffer, 0, got * static_cast<size_t>(outCh) * sizeof(float));

        switch (mode_) {

            case ChannelMixNode::MixMode::StereoToMono:
                // (L + R) * 0.5 — equal-power passive sum
                for (size_t f = 0; f < got; ++f)
                    buffer[f] = (scratch_[f * 2] + scratch_[f * 2 + 1]) * 0.5f;
                break;

            case ChannelMixNode::MixMode::MonoToStereo:
                // Duplicate mono source to both channels
                for (size_t f = 0; f < got; ++f) {
                    buffer[f * 2]     = scratch_[f];
                    buffer[f * 2 + 1] = scratch_[f];
                }
                break;

            case ChannelMixNode::MixMode::ToMono: {
                // Equal-weight average across all input channels
                const float weight = 1.0f / static_cast<float>(inChannels_);
                for (size_t f = 0; f < got; ++f) {
                    float sum = 0.0f;
                    for (int c = 0; c < inChannels_; ++c)
                        sum += scratch_[f * static_cast<size_t>(inChannels_) + c];
                    buffer[f] = sum * weight;
                }
                break;
            }

            case ChannelMixNode::MixMode::SurroundToStereo:
                if (inChannels_ == 6) {
                    // 5.1 — SMPTE channel order: L=0, R=1, C=2, LFE=3, Ls=4, Rs=5
                    // LFE is omitted (subsonic content is destructive in stereo).
                    for (size_t f = 0; f < got; ++f) {
                        const float* s = scratch_.data() + f * 6;
                        buffer[f * 2]     = s[0] + s[2] * kCenter + s[4] * kSurround;
                        buffer[f * 2 + 1] = s[1] + s[2] * kCenter + s[5] * kSurround;
                    }
                } else {
                    // 7.1 — SMPTE order: L=0, R=1, C=2, LFE=3, Ls=4, Rs=5, Lrs=6, Rrs=7
                    for (size_t f = 0; f < got; ++f) {
                        const float* s = scratch_.data() + f * 8;
                        buffer[f * 2]     = s[0] + s[2]*kCenter + s[4]*kSurround + s[6]*kRear;
                        buffer[f * 2 + 1] = s[1] + s[2]*kCenter + s[5]*kSurround + s[7]*kRear;
                    }
                }
                break;
        }

        return got;
    }

    const AudioFormat& format() const override { return outFmt_; }

private:
    std::unique_ptr<AudioStream> upstream_;
    ChannelMixNode::MixMode      mode_;
    int                          inChannels_;
    AudioFormat                  outFmt_;   // channels field overridden to output count
    std::vector<float>           scratch_;  // upstream read buffer, resized on demand
};

}  // anonymous namespace

// ============================================================================
// ChannelMixNode
// ============================================================================

void ChannelMixNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("mode");
    if (it == params.end()) return;

    const std::string& m = it->second;
    if      (m == "stereo_to_mono")     mode_ = MixMode::StereoToMono;
    else if (m == "mono_to_stereo")     mode_ = MixMode::MonoToStereo;
    else if (m == "surround_to_stereo") mode_ = MixMode::SurroundToStereo;
    else if (m == "to_mono")            mode_ = MixMode::ToMono;
    else throw std::invalid_argument("[ChannelMix] Unknown mode: " + m);

    auto pt = params.find("pass_through_on_mismatch");
    if (pt != params.end())
        passThroughOnInputChannelMismatch_ = (pt->second == "true" || pt->second == "1");
}

std::unique_ptr<AudioStream> ChannelMixNode::wrap(
    std::unique_ptr<AudioStream> upstream,
    NodeContext& /*ctx*/)
{
    const int inCh = upstream->format().channels;
    if (passThroughOnInputChannelMismatch_) {
        try {
            validateChannels(mode_, inCh);
        } catch (const std::runtime_error& e) {
            // TODO: Write to log system. Also consider feeding this message to a dedicated warning variable (like errorMsgNodeChain)
            LOG_WARN("ChannelMix", std::string(e.what()) + " -- passing through unchanged.");

            // Directy return the upstream stream as-is, without wrapping it in a ChannelMixStream,
            // effectively bypassing this node's processing.
            return upstream;
        }
    } else {
        validateChannels(mode_, inCh);
    }
    return std::make_unique<ChannelMixStream>(std::move(upstream), mode_, inCh);
}

// ----------------------------------------------------------------------------
// UI
// ----------------------------------------------------------------------------

static const char* const kModeLabels[4] = {
    "Stereo -> Mono   (2->1)",
    "Mono -> Stereo   (1->2)",
    "Surround -> Stereo   (5.1 / 7.1 -> 2)",
    "Any -> Mono   (N->1)",
};

static const char* const kModeHints[4] = {
    "(L + R) / 2  --  input: exactly 2 ch",
    "L = R = input  --  input: exactly 1 ch",
    "ITU-R BS.775 matrix, LFE omitted  --  input: 5.1 (6 ch) or 7.1 (8 ch)",
    "Equal-weight average  --  input: any channel count",
};

void ChannelMixNode::drawUI()
{
    const float labelWidth = ImGui::CalcTextSize("Mix Mode").x + 20.0f;

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Mix Mode");
    ImGui::SameLine(labelWidth);

    int current = static_cast<int>(mode_);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::Combo("##mix_mode", &current, kModeLabels, 4))
        mode_ = static_cast<MixMode>(current);

    ImGui::Spacing();
    ImGui::TextDisabled("%s", kModeHints[static_cast<int>(mode_)]);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::Checkbox("Pass through on input channel mismatch", &passThroughOnInputChannelMismatch_);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && ImGui::BeginItemTooltip())
    {
        ImGui::Text("Notice about this option:");
        ImGui::BulletText("When enabled, if the upstream channel count is incompatible with the selected mode, the node will pass the stream through unchanged\n"
                          "instead of treating it as an error.");
        ImGui::BulletText("When disabled, a channel count mismatch will throw an error, useful for checking your input file's channel format.");
        ImGui::Separator();
        ImGui::Text("This option can be useful to prevent the entire chain from breaking due to a channel count mismatch, "
                    "especially when the node is used in a\n"
                    "non-critical part of the chain or when the input format is uncertain.");
        ImGui::EndTooltip();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Hover on the checkbox for more details)");
}
