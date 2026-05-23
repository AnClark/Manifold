#include "DynamicRangeNode.hpp"
#include "pipeline/NodeRegistry.hpp"
#include "utils/LogManager.hpp"

#include <imgui.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

// --------------------------------------------------------------------------
// Node registration
// --------------------------------------------------------------------------
//
// REGISTER_NODE runs at static-init time (before main()) and inserts this
// node type into the NodeRegistry singleton.  The registration stores:
//   - a factory lambda used by NodeRegistry::create("dynamic_range")
//   - metadata (id / displayName / description / category / role) for the UI
// --------------------------------------------------------------------------

REGISTER_NODE(
    DynamicRangeNode,
    "dynamic_range",
    "Dynamic Range Analyzer",
    "Measures the TT Dynamic Range (DR value) and Crest Factor — key indicators "
    "of dynamic compression. Compatible with fre:ac, Picard, and Quod Libet DR meters.",
    "Analysis",
    NodeRole::StreamProcessor);

// --------------------------------------------------------------------------
// DynamicRangeReport::summary() / details()
// --------------------------------------------------------------------------

std::string DynamicRangeReport::summary() const
{
    if (!hasResult()) return "\xe2\x80\x94"; // em dash
    char buf[48];
    std::snprintf(buf, sizeof(buf), "DR%.0f  /  CF %.1f dB", drValue, crestFactorDb);
    return buf;
}

std::string DynamicRangeReport::details() const
{
    if (!hasResult())
        return "Not enough audio data to compute DR (< 1 complete block / < 3 s).";

    const char* quality =
        drValue >= 14.0 ? "good dynamic range" :
        drValue >= 8.0  ? "compressed"          :
                          "heavily compressed / over-limited";

    char buf[200];
    std::snprintf(buf, sizeof(buf),
        "DR%.0f  |  Crest: %.1f dB  |  %d block%s  [%s]",
        drValue,
        crestFactorDb,
        blocksAnalyzed,
        blocksAnalyzed == 1 ? "" : "s",
        quality);
    return buf;
}

// --------------------------------------------------------------------------
// DynamicRangeStream
// --------------------------------------------------------------------------
//
// ## Block-based accumulation
//
// The TT DR algorithm divides audio into fixed-length blocks (default 3 s).
// Because onSamples() delivers frames in arbitrary chunk sizes from upstream,
// block boundaries can fall anywhere inside a delivered buffer.  We therefore
// process chunks of frames, filling the current block up to blockSize_ and
// flushing it when full — see accumulateFrames() + flushBlock().
//
// ## Partial last block
//
// The last block may be shorter than 3 s.  We include it if it contains at
// least blockSize_/3 frames (~1 s) to avoid discarding meaningful data from
// files that are not a perfect multiple of 3 s.  Very short remnants (< 1 s)
// are excluded because they would produce a biased RMS estimate.
//
// ## Crest Factor vs DR
//
//   DR           uses only the top-20% block RMS → sensitive to sustained loud passages.
//   Crest Factor uses the whole-file RMS          → sensitive to average energy.
//   A heavily limited file has both a low DR and a low Crest Factor.
// --------------------------------------------------------------------------

namespace {

class DynamicRangeStream : public AnalyzerStream {
public:
    DynamicRangeStream(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx,
        float blockDurationSec)
        : AnalyzerStream(std::move(upstream), ctx)
    {
        const AudioFormat& fmt = upstream_->format();
        channels_  = fmt.channels;
        blockSize_ = static_cast<size_t>(
            std::llround(static_cast<double>(blockDurationSec)
                         * static_cast<double>(fmt.sampleRate)));
        if (blockSize_ == 0)
            blockSize_ = 1;   // guard against degenerate sample-rate

        // Per-channel running accumulators
        blockRms2_.assign(channels_, 0.0);  // sum-of-squares for current block
        totalRms2_.assign(channels_, 0.0);  // sum-of-squares for whole file
        peak_.assign(channels_, 0.0);       // absolute peak for whole file

        // Per-channel list of completed block RMS values
        blockRmsLists_.resize(channels_);
    }

protected:
    void onSamples(const float* buf, size_t frames) override
    {
        size_t offset    = 0;
        size_t remaining = frames;

        while (remaining > 0) {
            // Fill the current block up to blockSize_
            const size_t canFit = blockSize_ - framesInBlock_;
            const size_t take   = std::min(remaining, canFit);

            accumulateFrames(buf + offset * static_cast<size_t>(channels_), take);

            framesInBlock_ += take;
            offset         += take;
            remaining      -= take;
            totalFrames_   += take;

            if (framesInBlock_ == blockSize_)
                flushBlock();
        }
    }

private:
    // ------------------------------------------------------------------
    // Block helpers
    // ------------------------------------------------------------------

    /// Accumulate RMS² and peak for `frames` interleaved frames.
    void accumulateFrames(const float* buf, size_t frames)
    {
        const size_t total = frames * static_cast<size_t>(channels_);
        for (size_t i = 0; i < total; ++i) {
            const int    ch     = static_cast<int>(i % static_cast<size_t>(channels_));
            const double s      = static_cast<double>(buf[i]);
            const double abs_s  = s < 0.0 ? -s : s;

            blockRms2_[ch] += s * s;
            totalRms2_[ch] += s * s;
            if (abs_s > peak_[ch])
                peak_[ch] = abs_s;
        }
    }

    /// Commit the current block: compute per-channel RMS, append to blockRmsLists_,
    /// then reset the per-block accumulators.
    void flushBlock()
    {
        for (int c = 0; c < channels_; ++c) {
            const double rms = std::sqrt(blockRms2_[c] / static_cast<double>(blockSize_));
            blockRmsLists_[c].push_back(rms);
            blockRms2_[c] = 0.0;
        }
        framesInBlock_ = 0;
    }

    /// Flush the partial last block, using `framesInBlock_` as the denominator.
    /// Only called if framesInBlock_ >= blockSize_/3 (≈ 1 s of a 3-s block).
    void flushPartialBlock()
    {
        assert(framesInBlock_ > 0);
        for (int c = 0; c < channels_; ++c) {
            const double rms = std::sqrt(
                blockRms2_[c] / static_cast<double>(framesInBlock_));
            blockRmsLists_[c].push_back(rms);
        }
    }

    // ------------------------------------------------------------------
    // Finalization — DR + Crest Factor calculation
    // ------------------------------------------------------------------

    void onFinalize() override
    {
        // Include the partial last block if it is at least 1/3 of a full block.
        if (framesInBlock_ >= blockSize_ / 3 && framesInBlock_ > 0)
            flushPartialBlock();

        const int numBlocks = blockRmsLists_.empty()
            ? 0
            : static_cast<int>(blockRmsLists_[0].size());

        auto report = std::make_shared<DynamicRangeReport>();
        report->sourcePath = ctx_.sourcePath;

        if (numBlocks < 1 || totalFrames_ == 0) {
            LOG_WARNF("DynamicRange",
                "File '%s': not enough audio data to compute DR "
                "(need at least one 3-second block).",
                ctx_.sourcePath.c_str());
            if (ctx_.onReport)
                ctx_.onReport(std::move(report));
            return;
        }

        report->blocksAnalyzed = numBlocks;
        report->perChannelDr.resize(channels_);
        report->perChannelCrestFactor.resize(channels_);
        report->perChannelPeakDbfs.resize(channels_);

        // Number of "top" blocks for the DR reference RMS:
        //   top 20%, with a minimum of 2 (matches the TT DR spec).
        //   We also cap at numBlocks to avoid requesting more than available.
        const int topCount = std::min(
            numBlocks,
            std::max(2, static_cast<int>(std::ceil(numBlocks * 0.20))));

        double drSum    = 0.0;
        double crestSum = 0.0;

        for (int c = 0; c < channels_; ++c) {
            auto& rmsVals = blockRmsLists_[c];

            // Sort descending so rmsVals[0] is the loudest block.
            std::sort(rmsVals.begin(), rmsVals.end(), std::greater<double>());

            // Average of the top N blocks.
            double topRmsSum = 0.0;
            for (int i = 0; i < topCount; ++i)
                topRmsSum += rmsVals[i];
            const double avgTopRms = topRmsSum / static_cast<double>(topCount);

            const double peak = peak_[c];

            // Peak in dBFS
            report->perChannelPeakDbfs[c] = (peak > 0.0)
                ? 20.0 * std::log10(peak)
                : -std::numeric_limits<double>::infinity();

            // DR per channel = 20×log10(peak / avg_top_rms).
            // Infinity is returned when the channel is completely silent.
            report->perChannelDr[c] = (peak > 0.0 && avgTopRms > 0.0)
                ? 20.0 * std::log10(peak / avgTopRms)
                : std::numeric_limits<double>::infinity();

            // Crest Factor = 20×log10(peak / overall_rms).
            const double overallRms = (totalFrames_ > 0)
                ? std::sqrt(totalRms2_[c] / static_cast<double>(totalFrames_))
                : 0.0;
            report->perChannelCrestFactor[c] = (peak > 0.0 && overallRms > 0.0)
                ? 20.0 * std::log10(peak / overallRms)
                : std::numeric_limits<double>::infinity();

            drSum    += report->perChannelDr[c];
            crestSum += report->perChannelCrestFactor[c];
        }

        // Overall DR = floor of the per-channel average (standard convention).
        report->drValue       = std::floor(drSum    / static_cast<double>(channels_));
        report->crestFactorDb = crestSum / static_cast<double>(channels_);

        // Publish sidebands for downstream nodes.
        ctx_.setSideband("dr_value",        report->drValue);
        ctx_.setSideband("crest_factor_db", report->crestFactorDb);

        if (ctx_.onReport)
            ctx_.onReport(std::move(report));
    }

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------

    int    channels_      = 1;
    size_t blockSize_     = 0;    ///< Frames per block (sampleRate × blockDurationSec)
    size_t framesInBlock_ = 0;    ///< Frames accumulated in the current (open) block
    size_t totalFrames_   = 0;    ///< Total frames seen across the whole file

    std::vector<double> blockRms2_;  ///< Current block: sum-of-squares per channel
    std::vector<double> totalRms2_;  ///< Whole file:    sum-of-squares per channel
    std::vector<double> peak_;       ///< Whole file:    absolute peak per channel

    /// Completed block RMS values: blockRmsLists_[channel] = {rms0, rms1, ...}
    std::vector<std::vector<double>> blockRmsLists_;
};

} // anonymous namespace

// --------------------------------------------------------------------------
// init / exportConfig
// --------------------------------------------------------------------------

void DynamicRangeNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("block_duration");
    if (it != params.end()) {
        try {
            const float v = std::stof(it->second);
            if (v >= 0.5f && v <= 30.0f)
                blockDurationSec_ = v;
        } catch (...) {}
    }
}

std::unordered_map<std::string, std::string> DynamicRangeNode::exportConfig()
{
    // Only persist block_duration if it was changed from the standard 3 s.
    if (blockDurationSec_ == 3.0f)
        return {};
    return {{"block_duration", std::to_string(blockDurationSec_)}};
}

// --------------------------------------------------------------------------
// wrap
// --------------------------------------------------------------------------
//
// Called by ChainEngine once per file.  A fresh DynamicRangeStream is created
// for every file; the node itself remains stateless between runs.
// --------------------------------------------------------------------------

std::unique_ptr<AudioStream> DynamicRangeNode::wrap(
    std::unique_ptr<AudioStream> upstream,
    NodeContext& ctx)
{
    return std::make_unique<DynamicRangeStream>(
        std::move(upstream), ctx, blockDurationSec_);
}

// --------------------------------------------------------------------------
// drawUI
// --------------------------------------------------------------------------

void DynamicRangeNode::drawUI()
{
    const float labelWidth = ImGui::CalcTextSize("Block Duration").x + 20.0f;

    // Block duration slider
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Block Duration");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##block_dur", &blockDurationSec_, 1.0f, 10.0f, "%.0f s");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)
        && ImGui::BeginTooltip())
    {
        ImGui::Text("Duration of each analysis block.");
        ImGui::BulletText("3 s — TT DR standard (default, compatible with fre:ac / Picard).");
        ImGui::BulletText("Shorter blocks increase block count but reduce per-block RMS accuracy.");
        ImGui::EndTooltip();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // DR quality reference
    ImGui::TextDisabled("DR reference (top 20%% blocks):");
    ImGui::BulletText("DR >= 14  — good dynamic range");
    ImGui::BulletText("DR  8-13  — compressed");
    ImGui::BulletText("DR <  8   — heavily compressed / over-limited");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::BeginDisabled();
    ImGui::TextWrapped("Results stored in Task records.");
    ImGui::EndDisabled();
}
