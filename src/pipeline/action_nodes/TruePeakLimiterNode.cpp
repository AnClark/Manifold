#include "TruePeakLimiterNode.hpp"
#include "processors/TruePeakLimiterProcessor.hpp"
#include "pipeline/NodeRegistry.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

REGISTER_NODE(
    TruePeakLimiterNode,
    "true_peak_limiter",
    "True Peak Limiter",
    "Attenuates the signal so that the 4x oversampled True Peak does not exceed the configured ceiling (dBTP).",
    "Dynamics",
    NodeRole::StreamProcessor);

// ============================================================================
// TruePeakLimiterStream
// ============================================================================

namespace {

/**
 * @brief Buffers the entire upstream, measures True Peak, then replays with gain.
 *
 * Two-pass in one wrap() call:
 *   1. Drain upstream → interleaved float buffer
 *   2. De-interleave → call TruePeakLimiterProcessor::measureTruePeakDbtp()
 *   3. Compute attenuation gain (≤ 0 dB, never boost)
 *   4. read() serves from buffer scaled by that gain
 */
class TruePeakLimiterStream : public AudioStream {
public:
    TruePeakLimiterStream(std::unique_ptr<AudioStream> upstream, float tpCeiling)
        : format_(upstream->format())
    {
        const int ch = format_.channels;

        // --- Phase 1: drain upstream into interleaved buffer ---
        constexpr size_t kDrainBlock = 4096;
        std::vector<float> tmp(kDrainBlock * static_cast<size_t>(ch));
        size_t got;
        while ((got = upstream->read(tmp.data(), kDrainBlock)) > 0)
            buffer_.insert(buffer_.end(), tmp.data(),
                           tmp.data() + got * static_cast<size_t>(ch));

        // --- Phase 2: de-interleave and measure True Peak ---
        const size_t totalFrames = buffer_.size() / static_cast<size_t>(ch);
        std::vector<std::vector<float>> planar(ch, std::vector<float>(totalFrames));
        std::vector<const float*> ptrs(static_cast<size_t>(ch));
        for (int c = 0; c < ch; ++c) {
            for (size_t f = 0; f < totalFrames; ++f)
                planar[c][f] = buffer_[f * static_cast<size_t>(ch) + static_cast<size_t>(c)];
            ptrs[static_cast<size_t>(c)] = planar[c].data();
        }

        const double measuredTp = TruePeakLimiterProcessor::measureTruePeakDbtp(
            ptrs.data(), ch, totalFrames);

        // --- Phase 3: compute attenuation (only attenuate, never boost) ---
        const float gainDb = std::min(0.0f, tpCeiling - static_cast<float>(measuredTp));
        gainLinear_ = std::pow(10.0f, gainDb / 20.0f);
    }

    size_t read(float* out, size_t frames) override
    {
        const size_t ch     = static_cast<size_t>(format_.channels);
        const size_t avail  = (buffer_.size() - readPos_) / ch;
        const size_t toRead = std::min(frames, avail);
        if (toRead == 0) return 0;

        const size_t samples = toRead * ch;
        for (size_t i = 0; i < samples; ++i)
            out[i] = buffer_[readPos_ + i] * gainLinear_;
        readPos_ += samples;
        return toRead;
    }

    const AudioFormat& format() const override { return format_; }

private:
    std::vector<float> buffer_;
    size_t             readPos_    = 0;
    AudioFormat        format_;
    float              gainLinear_ = 1.0f;
};

}  // anonymous namespace

// ============================================================================
// TruePeakLimiterNode
// ============================================================================

TruePeakLimiterNode::TruePeakLimiterNode()
    : DSPNode(DSPNode::makeProcessor<TruePeakLimiterProcessor>(), "TruePeakLimiter")
{}

void TruePeakLimiterNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("tp_ceiling");
    if (it != params.end())
        tpCeiling_ = std::stof(it->second);
}

std::unique_ptr<AudioStream> TruePeakLimiterNode::wrap(
    std::unique_ptr<AudioStream> upstream,
    NodeContext& /*ctx*/)
{
    return std::make_unique<TruePeakLimiterStream>(std::move(upstream), tpCeiling_);
}

void TruePeakLimiterNode::drawUI()
{
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("TP Ceiling");
    ImGui::SameLine(0, 10.0f);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##tp_ceiling", &tpCeiling_, -9.0f, 0.0f, "%.1f dBTP");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("True Peak measured via 4\xc3\x97 oversampling (r8brain).");
}

