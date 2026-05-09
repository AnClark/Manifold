#include "FadeNode.hpp"
#include "pipeline/NodeRegistry.hpp"
#include "utils/LogManager.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

REGISTER_NODE(
    FadeNode,
    "fade",
    "Fade",
    "Applies fade-in and/or fade-out envelopes with selectable curve shapes (Linear, Sine, Exponential).",
    "Edit",
    NodeRole::StreamProcessor);

// ============================================================================
// FadeStream
// ============================================================================

namespace {

static constexpr float kPi = 3.14159265358979323846f;

/**
 * @brief Evaluate the gain envelope at normalised position t ∈ [0, 1].
 *
 * t = 0 → silence, t = 1 → full level (used for both fade-in and fade-out;
 * the caller reverses t for fade-out).
 */
float evalCurve(float t, FadeNode::CurveType curve)
{
    switch (curve) {
        case FadeNode::CurveType::Linear:      return t;
        case FadeNode::CurveType::Sine:        return std::sin(t * kPi * 0.5f);
        case FadeNode::CurveType::Exponential: return t * t;
    }
    return t;
}

/**
 * @brief Buffers the entire upstream and replays it with fade envelopes applied.
 *
 * Construction (one pass):
 *   1. Drain upstream → interleaved float buffer
 *   2. Clamp fade-in / fade-out frame counts to at most half of totalFrames_
 *      (prevents the two fades from overlapping)
 *
 * read() applies envelope gain on-the-fly during replay — no second write
 * pass over the buffer is needed.
 */
class FadeStream : public AudioStream {
public:
    FadeStream(std::unique_ptr<AudioStream> upstream,
               float fadeInMs,   FadeNode::CurveType fadeInCurve,
               float fadeOutMs,  FadeNode::CurveType fadeOutCurve,
               const std::string& sourcePath)
        : format_(upstream->format())
        , fadeInCurve_(fadeInCurve)
        , fadeOutCurve_(fadeOutCurve)
    {
        const size_t ch = static_cast<size_t>(format_.channels);
        const double sr = format_.sampleRate;

        // ---- Phase 1: drain upstream ----------------------------------------
        constexpr size_t kDrainBlock = 4096;
        std::vector<float> tmp(kDrainBlock * ch);
        size_t got;
        while ((got = upstream->read(tmp.data(), kDrainBlock)) > 0)
            buffer_.insert(buffer_.end(),
                           tmp.data(),
                           tmp.data() + got * ch);

        totalFrames_ = buffer_.size() / ch;

        if (totalFrames_ == 0) {
            LOG_ERRORF("Fade", "Audio stream contains no frames: %s", sourcePath.c_str());
            throw std::runtime_error(
                "[Fade] Audio stream contains no frames: " + sourcePath);
        }

        // ---- Phase 2: compute fade frame counts ----------------------------
        // Clamp each side to half of total so they can never overlap
        const size_t halfFrames = totalFrames_ / 2;

        fadeInFrames_  = std::min(
            static_cast<size_t>(fadeInMs  / 1000.0 * sr + 0.5), halfFrames);
        fadeOutFrames_ = std::min(
            static_cast<size_t>(fadeOutMs / 1000.0 * sr + 0.5), halfFrames);

        LOG_DEBUGF("Fade",
                   "fade-in %.0f ms (%zu frames) [%s] | fade-out %.0f ms (%zu frames) [%s] | %s",
                   fadeInMs,  fadeInFrames_,  curveToString(fadeInCurve),
                   fadeOutMs, fadeOutFrames_, curveToString(fadeOutCurve),
                   sourcePath.c_str());
    }

    size_t read(float* out, size_t frames) override
    {
        const size_t ch     = static_cast<size_t>(format_.channels);
        const size_t avail  = (buffer_.size() - readPos_) / ch;
        const size_t toRead = std::min(frames, avail);
        if (toRead == 0) return 0;

        const size_t samples = toRead * ch;
        std::memcpy(out, buffer_.data() + readPos_, samples * sizeof(float));

        // Apply per-frame envelope gain in-place
        const size_t frameOffset   = readPos_ / ch;
        const size_t fadeOutStart  = totalFrames_ - fadeOutFrames_;

        for (size_t i = 0; i < toRead; ++i) {
            const size_t f = frameOffset + i;
            float gain = 1.0f;

            if (fadeInFrames_ > 0 && f < fadeInFrames_) {
                const float t = static_cast<float>(f) / static_cast<float>(fadeInFrames_);
                gain = evalCurve(t, fadeInCurve_);
            }

            if (fadeOutFrames_ > 0 && f >= fadeOutStart) {
                // t runs from 1 (start of fade-out) down to 0 (last frame)
                const float t = static_cast<float>(totalFrames_ - 1 - f)
                              / static_cast<float>(fadeOutFrames_);
                const float fadeOutGain = evalCurve(t, fadeOutCurve_);
                gain = std::min(gain, fadeOutGain);
            }

            float* frame = out + i * ch;
            for (size_t c = 0; c < ch; ++c)
                frame[c] *= gain;
        }

        readPos_ += samples;
        return toRead;
    }

    const AudioFormat& format() const override { return format_; }

private:
    static const char* curveToString(FadeNode::CurveType curve)
    {
        switch (curve) {
            case FadeNode::CurveType::Linear:      return "Linear";
            case FadeNode::CurveType::Sine:        return "Sine";
            case FadeNode::CurveType::Exponential: return "Exponential";
        }
        return "?";
    }

    std::vector<float>  buffer_;
    size_t              readPos_       = 0;
    size_t              totalFrames_   = 0;
    size_t              fadeInFrames_  = 0;
    size_t              fadeOutFrames_ = 0;
    FadeNode::CurveType fadeInCurve_;
    FadeNode::CurveType fadeOutCurve_;
    AudioFormat         format_;
};

// --------------------------------------------------------------------------
// Helpers shared between init() and drawUI()
// --------------------------------------------------------------------------

FadeNode::CurveType curveFromString(const std::string& s)
{
    if (s == "linear")      return FadeNode::CurveType::Linear;
    if (s == "sine")        return FadeNode::CurveType::Sine;
    if (s == "exponential") return FadeNode::CurveType::Exponential;
    throw std::invalid_argument("[Fade] Unknown curve type: " + s);
}

}  // anonymous namespace

// ============================================================================
// FadeNode
// ============================================================================

void FadeNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("fade_in_ms");
    if (it != params.end()) {
        try { fadeInMs_ = std::stof(it->second); }
        catch (...) {
            throw std::invalid_argument(
                "[Fade] Invalid value for 'fade_in_ms': " + it->second);
        }
    }

    it = params.find("fade_out_ms");
    if (it != params.end()) {
        try { fadeOutMs_ = std::stof(it->second); }
        catch (...) {
            throw std::invalid_argument(
                "[Fade] Invalid value for 'fade_out_ms': " + it->second);
        }
    }

    it = params.find("fade_in_curve");
    if (it != params.end())
        fadeInCurve_ = curveFromString(it->second);

    it = params.find("fade_out_curve");
    if (it != params.end())
        fadeOutCurve_ = curveFromString(it->second);
}

std::unique_ptr<AudioStream> FadeNode::wrap(
    std::unique_ptr<AudioStream> upstream,
    NodeContext& ctx)
{
    return std::make_unique<FadeStream>(
        std::move(upstream),
        fadeInMs_,  fadeInCurve_,
        fadeOutMs_, fadeOutCurve_,
        ctx.sourcePath);
}

void FadeNode::drawUI()
{
    static const char* kCurveNames[] = { "Linear", "Sine (Equal-Power)", "Exponential" };

    const float labelWidth = ImGui::CalcTextSize("Fade-out Duration").x + 20.0f;

    // ---- Fade In -------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Fade In");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Duration");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##fade_in_ms", &fadeInMs_, 0.0f, 30000.0f, "%.0f ms");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Curve");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    int fadeInCurveIdx = static_cast<int>(fadeInCurve_);
    if (ImGui::Combo("##fade_in_curve", &fadeInCurveIdx, kCurveNames, 3))
        fadeInCurve_ = static_cast<CurveType>(fadeInCurveIdx);

    // ---- Fade Out ------------------------------------------------------
    ImGui::Spacing();
    ImGui::SeparatorText("Fade Out");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Duration");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##fade_out_ms", &fadeOutMs_, 0.0f, 30000.0f, "%.0f ms");

    ImGui::AlignTextToFramePadding();
    ImGui::Text("Curve");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    int fadeOutCurveIdx = static_cast<int>(fadeOutCurve_);
    if (ImGui::Combo("##fade_out_curve", &fadeOutCurveIdx, kCurveNames, 3))
        fadeOutCurve_ = static_cast<CurveType>(fadeOutCurveIdx);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Set duration to 0 to disable a side.");
}
