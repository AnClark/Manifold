#include "TrimSilenceNode.hpp"
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
    TrimSilenceNode,
    "trim_silence",
    "Trim Silence",
    "Removes leading and trailing silence from audio using a configurable dBFS threshold and padding.",
    "Edit",
    NodeRole::StreamProcessor);

// ============================================================================
// TrimSilenceStream
// ============================================================================

namespace {

/**
 * @brief Buffers the entire upstream, detects content boundaries, then
 *        replays only the non-silent region (plus padding).
 *
 * Two-pass in one constructor call:
 *   1. Drain upstream → interleaved float buffer
 *   2. Scan forward  → first non-silent frame (startFrame)
 *   3. Scan backward → last  non-silent frame (endFrame)
 *   4. Expand both ends by paddingFrames and clamp to buffer bounds
 *   5. read() serves frames from [startSample_, endSample_)
 */
class TrimSilenceStream : public AudioStream {
public:
    TrimSilenceStream(std::unique_ptr<AudioStream> upstream,
                      float thresholdDbfs,
                      float paddingMs,
                      const std::string& sourcePath)
        : format_(upstream->format())
    {
        const int    ch = format_.channels;
        const double sr = format_.sampleRate;

        // ---- Phase 1: drain upstream ----------------------------------------
        constexpr size_t kDrainBlock = 4096;
        std::vector<float> tmp(kDrainBlock * static_cast<size_t>(ch));
        size_t got;
        while ((got = upstream->read(tmp.data(), kDrainBlock)) > 0)
            buffer_.insert(buffer_.end(),
                           tmp.data(),
                           tmp.data() + got * static_cast<size_t>(ch));

        const size_t totalFrames = buffer_.size() / static_cast<size_t>(ch);

        if (totalFrames == 0) {
            LOG_ERRORF("TrimSilence", "Audio stream contains no frames: %s",
                       sourcePath.c_str());
            throw std::runtime_error(
                "[TrimSilence] Audio stream contains no frames: " + sourcePath);
        }

        // ---- Phase 2: detect content boundaries -----------------------------
        const float thresholdLinear = std::pow(10.0f, thresholdDbfs / 20.0f);

        auto frameIsSilent = [&](size_t f) -> bool {
            const size_t base = f * static_cast<size_t>(ch);
            for (int c = 0; c < ch; ++c)
                if (std::abs(buffer_[base + static_cast<size_t>(c)]) >= thresholdLinear)
                    return false;
            return true;
        };

        // Scan forward for first non-silent frame
        size_t startFrame = 0;
        while (startFrame < totalFrames && frameIsSilent(startFrame))
            ++startFrame;

        if (startFrame == totalFrames) {
            // Entire stream is silent — warn and return the audio unchanged
            LOG_WARNF("TrimSilence",
                      "All frames are below threshold (%.1f dBFS); no trimming applied: %s",
                      thresholdDbfs, sourcePath.c_str());
            startSample_ = 0;
            endSample_   = buffer_.size();
            readPos_     = 0;
            return;
        }

        // Scan backward for last non-silent frame
        size_t endFrame = totalFrames - 1;
        while (endFrame > startFrame && frameIsSilent(endFrame))
            --endFrame;

        // ---- Phase 3: apply padding -----------------------------------------
        const size_t paddingFrames = static_cast<size_t>(
            static_cast<double>(paddingMs) / 1000.0 * sr + 0.5);

        startFrame = (startFrame >= paddingFrames) ? startFrame - paddingFrames : 0;
        endFrame   = std::min(endFrame + paddingFrames, totalFrames - 1);

        startSample_ = startFrame * static_cast<size_t>(ch);
        endSample_   = (endFrame + 1) * static_cast<size_t>(ch);  // exclusive
        readPos_     = startSample_;

        LOG_DEBUGF("TrimSilence",
                   "Trimmed %.0f\xe2\x80\x93%.0f ms (of %.0f ms) | threshold %.1f dBFS | padding %.0f ms | %s",
                   static_cast<double>(startFrame) / sr * 1000.0,
                   static_cast<double>(endFrame + 1) / sr * 1000.0,
                   static_cast<double>(totalFrames) / sr * 1000.0,
                   thresholdDbfs,
                   paddingMs,
                   sourcePath.c_str());
    }

    size_t read(float* out, size_t frames) override
    {
        const size_t ch    = static_cast<size_t>(format_.channels);
        const size_t avail = (endSample_ - readPos_) / ch;
        const size_t toRead = std::min(frames, avail);
        if (toRead == 0) return 0;

        const size_t samples = toRead * ch;
        std::memcpy(out, buffer_.data() + readPos_, samples * sizeof(float));
        readPos_ += samples;
        return toRead;
    }

    const AudioFormat& format() const override { return format_; }

private:
    std::vector<float> buffer_;
    size_t             startSample_ = 0;
    size_t             endSample_   = 0;
    size_t             readPos_     = 0;
    AudioFormat        format_;
};

}  // anonymous namespace

// ============================================================================
// TrimSilenceNode
// ============================================================================

void TrimSilenceNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("threshold_dbfs");
    if (it != params.end()) {
        try { thresholdDbfs_ = std::stof(it->second); }
        catch (...) {
            throw std::invalid_argument(
                "[TrimSilence] Invalid value for 'threshold_dbfs': " + it->second);
        }
    }

    it = params.find("padding_ms");
    if (it != params.end()) {
        try { paddingMs_ = std::stof(it->second); }
        catch (...) {
            throw std::invalid_argument(
                "[TrimSilence] Invalid value for 'padding_ms': " + it->second);
        }
    }
}

std::unique_ptr<AudioStream> TrimSilenceNode::wrap(
    std::unique_ptr<AudioStream> upstream,
    NodeContext& ctx)
{
    return std::make_unique<TrimSilenceStream>(
        std::move(upstream),
        thresholdDbfs_,
        paddingMs_,
        ctx.sourcePath);
}

void TrimSilenceNode::drawUI()
{
    const float labelWidth = ImGui::CalcTextSize("Silence Threshold").x + 20.0f;

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Silence Threshold");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##threshold_dbfs", &thresholdDbfs_, -96.0f, 0.0f, "%.1f dBFS");

    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Padding");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##padding_ms", &paddingMs_, 0.0f, 500.0f, "%.0f ms");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("Trims leading and trailing silence from each file.");
}
