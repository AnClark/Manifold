#include "ClippingDetectionNode.hpp"
#include "pipeline/NodeRegistry.hpp"
#include "utils/LogManager.hpp"
#include "utils/SfOpenUtf8.hpp"

#include <sndfile.h>
#include <imgui.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

// --------------------------------------------------------------------------
// Node registration
// --------------------------------------------------------------------------
//
// REGISTER_NODE runs at static-init time (before main()) and inserts this
// node type into the NodeRegistry singleton.  The registration stores:
//   - a factory lambda used by NodeRegistry::create("clipping_detection")
//   - metadata (id / displayName / description / category / role) for the UI
//
// NodeRole::StreamProcessor tells ChainEngine to call wrap() once per file,
// inserting a fresh ClippingDetectionStream into the decorator chain.
// --------------------------------------------------------------------------

REGISTER_NODE(
    ClippingDetectionNode,
    "clipping_detection",
    "Clipping Detection",
    "Scans the audio stream for samples that exceed a dBFS threshold and reports a pass/fail result.",
    "Analysis",
    NodeRole::StreamProcessor);

// --------------------------------------------------------------------------
// ClippingDetectionReport::summary()
// --------------------------------------------------------------------------
//
// Produces a single-line human-readable summary for compact display in the
// UI report list.  Three cases:
//   1. totalSamples == 0 — Offline open failed or the stream was empty
//   2. clippedSamples == 0 — scan completed with no clipping found → PASS
//   3. clipped samples present — show count, ppm rate, peak level, and verdict
// --------------------------------------------------------------------------

std::string ClippingDetectionReport::summary() const
{
    char buf[200];

    if (totalSamples == 0) {
        std::snprintf(buf, sizeof(buf), "No samples scanned.");
        return buf;
    }

    if (clippedSamples == 0) {
        std::snprintf(buf, sizeof(buf),
            "No clipping detected (threshold %.1f dBFS)  [PASS]",
            thresholdDbfs);
    } else {
        std::snprintf(buf, sizeof(buf),
            "%lld clipped samples (%.2f ppm)  |  Peak: %.2f dBFS  [%s]",
            static_cast<long long>(clippedSamples),
            clippingRatePpm,
            maxSampleDbfs,
            passed() ? "PASS" : "FAIL");
    }
    return buf;
}

// --------------------------------------------------------------------------
// ClippingDetectionStream
// --------------------------------------------------------------------------
//
// The heart of this node: a transparent pass-through AudioStream decorator.
//
// ## Decorator pattern
//
// Each StreamProcessorNode creates a new AudioStream in wrap() that holds a
// unique_ptr to its upstream.  When the Sink drives its pull loop, read()
// calls propagate upward recursively:
//
//   OutputSinkNode::consume()
//     └─ ClippingDetectionStream::read()     ← this class
//          └─ upstream_->read()              ← previous decorator (if any)
//               └─ AudioFileStream::read()   ← disk I/O at the source
//
// ## Data flow
//
// read() responsibilities:
//   1. Call upstream_->read(buf, frames) to fill the caller's buffer.
//   2. In Inline mode, call scanBuffer() while the samples are in the buffer.
//      The data itself is returned to the Sink completely unmodified.
//   3. When upstream_ returns 0 (EOF), call finalize() to emit the report.
//
// ## Why finalize at EOF rather than after each read?
//
// Clipping detection accumulates global statistics across the entire file
// (total sample count, peak level, per-channel mask).  These values are only
// complete once every frame has been scanned, so finalize() is deferred to
// the first read() that returns 0 — the same lifecycle point at which other
// analysis nodes (e.g. LoudnessAnalyzerNode) also commit their results.
//
// ## Offline mode special handling
//
// In Offline mode read() simply forwards data without scanning.  The actual
// scan runs inside finalize() via scanOffline(), which opens ctx_.sourcePath
// through a separate libsndfile handle.  This keeps the scan independent of
// any upstream processing applied to the live stream.
// --------------------------------------------------------------------------

namespace {

static constexpr const char* kBackendNames[] = { "Inline", "Offline" };
static constexpr int         kNumBackends    = 2;

// ---------------------------------------------------------------------------

class ClippingDetectionStream : public AnalyzerStream {
public:
    ClippingDetectionStream(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx,
        ClippingBackend backend,
        float thresholdDbfs,
        int64_t maxAllowedClipped)
        : AnalyzerStream(std::move(upstream), ctx)
        , backend_(backend)
        , thresholdDbfs_(thresholdDbfs)
        // Pre-convert the dBFS threshold to a linear amplitude so the hot
        // path (scanBuffer) only performs a plain float comparison.
        // Formula: linear = 10^(dBFS / 20).  Default 0.0 dBFS → 1.0f (full scale).
        , thresholdLinear_(std::pow(10.0f, thresholdDbfs / 20.0f))
        , maxAllowedClipped_(maxAllowedClipped)
    {}

protected:
    void onSamples(const float* buf, size_t frames) override
    {
        if (backend_ == ClippingBackend::Inline)
            scanBuffer(buf, frames);
    }

private:
    // ------------------------------------------------------------------
    // Sample scanning
    // ------------------------------------------------------------------
    //
    // scanBuffer() is the only hot path and must be kept lean.
    //   - All work is fabs + float compare; no expensive branch per sample.
    //   - thresholdLinear_ is pre-computed in the constructor.
    //   - clippedChannelMask_ is updated with a single bitwise OR per hit.
    //
    // Buffer layout: libsndfile and AudioStream both use interleaved format.
    // For stereo the memory order is [L0, R0, L1, R1, L2, R2, ...], so the
    // channel index for sample i is simply i % channels.
    //
    // clippedChannelMask_: bit 0 = CH0, bit 1 = CH1, etc.  Supports up to
    // 32 channels (uint32_t).  Files with more than 32 channels are rare
    // enough that no special handling is warranted.
    // ------------------------------------------------------------------

    void scanBuffer(const float* buf, size_t frames)
    {
        const int ch = upstream_->format().channels;
        const size_t n = frames * static_cast<size_t>(ch);

        for (size_t i = 0; i < n; ++i) {
            const float absVal = std::fabs(buf[i]);
            // Track the global absolute peak for the maxSampleDbfs report field.
            if (absVal > maxSeen_)
                maxSeen_ = absVal;
            // Use >= so a sample that lands exactly on the threshold is counted.
            if (absVal >= thresholdLinear_) {
                ++clippedSamples_;
                // Mark the channel this sample belongs to in the bitmask.
                //
                // Because the buffer is interleaved, sample index i maps to
                // channel (i % ch).  For example with ch=2 (stereo):
                //   i=0 → CH0 (L), i=1 → CH1 (R), i=2 → CH0 (L), …
                //
                // (1u << channel) produces a value with exactly one bit set:
                //   CH0 → 0b0001, CH1 → 0b0010, CH2 → 0b0100, …
                //
                // OR-ing that into clippedChannelMask_ permanently sets that
                // bit, recording "channel N had at least one clipped sample"
                // without needing a separate counter array per channel.
                clippedChannelMask_ |= (1u << (static_cast<unsigned>(i) % static_cast<unsigned>(ch)));
            }
        }
        totalSamples_ += static_cast<int64_t>(n);
    }

    // ------------------------------------------------------------------
    // Offline scan — open sourcePath independently via libsndfile
    // ------------------------------------------------------------------
    //
    // Motivation: sometimes the caller needs to inspect the original source
    // file rather than the processed stream — e.g. to check whether clipping
    // already existed before any normalization or limiting was applied, or to
    // run a QA pass that is entirely independent of the node chain position.
    //
    // Implementation: SfOpenUtf8() is a cross-platform UTF-8 path wrapper
    // around sf_open().  The file is read in 1-second chunks and each chunk
    // is passed to scanBuffer() so the same counting logic is reused.
    // A 1-second chunk size balances syscall overhead against memory usage
    // (worst case: 48 000 frames × 8 ch × 4 B ≈ 1.5 MB, heap-allocated).
    //
    // Note: scanOffline() is called from finalize() after upstream_ has
    // already reached EOF.  In Offline mode read() never calls scanBuffer(),
    // so all accumulators are still at their zero-initialised values when
    // scanOffline() runs — the final report therefore reflects only the
    // original source file.
    // ------------------------------------------------------------------

    void scanOffline()
    {
        if (ctx_.sourcePath.empty()) {
            LOG_ERRORF("ClippingDetection",
                "Offline: ctx.sourcePath is empty — cannot scan.");
            throw std::runtime_error("[ClippingDetection] Offline mode: source path is empty.");
        }

        SF_INFO sfInfo = {};
        SNDFILE* file = SfOpenUtf8(ctx_.sourcePath, SFM_READ, &sfInfo);
        if (!file) {
            const std::string sfErr = sf_strerror(nullptr);
            LOG_ERRORF("ClippingDetection",
                "Offline: cannot open '%s': %s",
                ctx_.sourcePath.c_str(), sfErr.c_str());
            throw std::runtime_error(
                "[ClippingDetection] Offline: cannot open '" + ctx_.sourcePath + "': " + sfErr);
        }

        const int ch          = sfInfo.channels;
        const size_t chunkSz  = static_cast<size_t>(sfInfo.samplerate) // frames per 1-second chunk
                                * static_cast<size_t>(ch);              // × channels = total samples
        std::vector<float> tmp(chunkSz);

        sf_count_t n;
        // sf_readf_float returns the number of frames actually read;
        // 0 or negative signals EOF or an error — either way the loop ends.
        while ((n = sf_readf_float(file, tmp.data(),
                    static_cast<sf_count_t>(sfInfo.samplerate))) > 0) {
            scanBuffer(tmp.data(), static_cast<size_t>(n));
        }

        sf_close(file);
    }

    // ------------------------------------------------------------------
    // Finalize: build + emit the report
    // ------------------------------------------------------------------
    //
    // Called once when read() first returns 0 (EOF); the finalized_ flag
    // prevents a second invocation if the sink calls read() again.
    //
    // Steps:
    //   1. Offline mode: run the deferred source-file scan now (scanOffline()).
    //      Inline mode:  all accumulation already happened inside read(); nothing to do.
    //   2. Write clippedSamples_ to NodeContext sideband under "clipping_count"
    //      so any downstream node (e.g. a future conditional-skip node) can query it.
    //   3. Populate and publish a ClippingDetectionReport via ctx_.onReport().
    //      The UI thread picks this up on the next refresh cycle.
    //
    // maxSampleDbfs conversion:
    //   maxSeen_ holds the largest absolute linear amplitude seen (0.0 … 1.0+).
    //   dBFS = 20 × log10(linear).  When maxSeen_ == 0 (silent file) log10(0)
    //   would be -inf; we represent that explicitly with
    //   -numeric_limits<double>::infinity() to avoid NaN propagation.
    //
    // clippingRatePpm (parts-per-million):
    //   ppm = clipped / total × 1 000 000.  For sparse clipping this is far
    //   more readable than percent: one clipped sample in a 48 kHz mono
    //   60-second file is ~0.35 ppm vs. 0.000035%, which is hard to parse.
    // ------------------------------------------------------------------

    void onFinalize() override
    {
        if (backend_ == ClippingBackend::Offline)
            scanOffline();

        // Write to sideband so downstream nodes can read "clipping_count" (int64_t).
        ctx_.setSideband("clipping_count", clippedSamples_);

        auto report = std::make_shared<ClippingDetectionReport>();
        report->sourcePath        = ctx_.sourcePath;
        report->totalSamples      = totalSamples_;
        report->clippedSamples    = clippedSamples_;
        report->clippingRatePpm   = (totalSamples_ > 0)
            ? static_cast<double>(clippedSamples_) / static_cast<double>(totalSamples_) * 1'000'000.0
            : 0.0;
        report->maxSampleDbfs     = (maxSeen_ > 0.0f)
            ? static_cast<double>(20.0f * std::log10(maxSeen_))
            : -std::numeric_limits<double>::infinity();
        report->clippedChannelMask = clippedChannelMask_;
        report->thresholdDbfs     = thresholdDbfs_;
        report->maxAllowedClipped = maxAllowedClipped_;

        if (ctx_.onReport)
            ctx_.onReport(std::move(report));
    }

    // ------------------------------------------------------------------
    // Members
    // ------------------------------------------------------------------

    ClippingBackend backend_;
    float           thresholdDbfs_;
    float           thresholdLinear_;
    int64_t         maxAllowedClipped_;

    // Accumulate per-file
    int64_t  totalSamples_       = 0;
    int64_t  clippedSamples_     = 0;
    float    maxSeen_            = 0.0f;
    uint32_t clippedChannelMask_ = 0;
};

} // anonymous namespace

// --------------------------------------------------------------------------
// wrap
// --------------------------------------------------------------------------
//
// Called by ChainEngine once per file to insert this node into the stream
// decorator chain.  A fresh ClippingDetectionStream is created each time,
// taking ownership of the upstream and receiving the node's current config
// parameters by value.
//
// The node itself (ClippingDetectionNode) is stateless across files — all
// per-file accumulators (totalSamples_, clippedSamples_, etc.) live inside
// ClippingDetectionStream.  Because wrap() produces a brand-new stream
// instance for every file, concurrent multi-file processing is safe with
// no additional locking.
// --------------------------------------------------------------------------

std::unique_ptr<AudioStream> ClippingDetectionNode::wrap(
    std::unique_ptr<AudioStream> upstream,
    NodeContext& ctx)
{
    return std::make_unique<ClippingDetectionStream>(
        std::move(upstream), ctx,
        backend_,
        thresholdDbfs_,
        static_cast<int64_t>(maxAllowedClipped_));
}

// --------------------------------------------------------------------------
// init / exportConfig
// --------------------------------------------------------------------------
//
// init() is called by ChainEngine / Config at chain-construction time to
// deserialise node parameters from a TOML config file.  Parameters arrive
// as a string-string map; the node is responsible for parsing and clamping.
//
// exportConfig() is the inverse: it serialises the current node state back
// into a string-string map so the Config system can persist it to TOML
// (the "save current chain" feature).
//
// Together they form the persistence interface that survives across sessions.
// --------------------------------------------------------------------------

void ClippingDetectionNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("backend");
    if (it != params.end())
        backend_ = (it->second == "offline") ? ClippingBackend::Offline : ClippingBackend::Inline;

    it = params.find("threshold_dbfs");
    if (it != params.end()) {
        try { thresholdDbfs_ = std::stof(it->second); } catch (...) {}
    }

    it = params.find("max_allowed_clipped");
    if (it != params.end()) {
        try { maxAllowedClipped_ = std::stoi(it->second); } catch (...) {}
    }
}

std::unordered_map<std::string, std::string> ClippingDetectionNode::exportConfig()
{
    return {
        { "backend",             (backend_ == ClippingBackend::Offline) ? "offline" : "inline" },
        { "threshold_dbfs",      std::to_string(thresholdDbfs_)    },
        { "max_allowed_clipped", std::to_string(maxAllowedClipped_) },
    };
}

// --------------------------------------------------------------------------
// drawUI
// --------------------------------------------------------------------------
//
// Called by the Action Editor panel when this node is selected.  Uses Dear
// ImGui immediate-mode API to render the parameter widgets.  All widgets
// write directly to the node's member variables; changes take effect
// immediately without a confirmation step.
//
// Layout convention (matches LoudnessComplianceNode):
//   - Fixed-width left column (labelWidth) for parameter labels
//   - SetNextItemWidth(-1.0f) lets each widget fill the remaining width
//   - Every widget has a DelayNormal tooltip for extended description
// --------------------------------------------------------------------------

void ClippingDetectionNode::drawUI()
{
    const float labelWidth = ImGui::CalcTextSize("Max Allowed Clipped").x + 20.0f;

    // Backend
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Backend");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    int backendIdx = static_cast<int>(backend_);
    if (ImGui::Combo("##backend", &backendIdx, kBackendNames, kNumBackends))
        backend_ = static_cast<ClippingBackend>(backendIdx);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)
        && ImGui::BeginTooltip()) {
        ImGui::Text("Instruction about the backends:");
        ImGui::BulletText("Inline  — scans the live stream as it passes through this node.\n"
                          "          Detects clipping in the post-processing signal.");
        ImGui::BulletText("Offline — opens the original source file independently.\n"
                          "          Unaffected by upstream processing nodes.");
        ImGui::EndTooltip();
    }

    // Threshold
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Threshold");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::SliderFloat("##threshold", &thresholdDbfs_, -3.0f, 0.0f, "%.1f dBFS");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)
        && ImGui::BeginTooltip()) {
        ImGui::Text("Samples at or above this level are counted as clipped.");
        ImGui::Text("0.0 dBFS = full scale (hard clip). Lower values detect near-clipping.");
        ImGui::EndTooltip();
    }

    // Max Allowed Clipped Samples
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Max Allowed Clipped");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputInt("##max_allowed", &maxAllowedClipped_, 1, 10);
    if (maxAllowedClipped_ < 0) maxAllowedClipped_ = 0;
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)
        && ImGui::BeginTooltip()) {
        ImGui::Text("Maximum clipped samples before the file is marked FAIL.");
        ImGui::Text("0 = zero tolerance.");
        ImGui::Text("A small value (e.g. 3) can tolerate true-peak quantisation artefacts.");
        ImGui::EndTooltip();
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::BeginDisabled();
    ImGui::TextWrapped("Results stored in Task records.\nOpen Tasks view, hover on the badge (which shows PASS or FAIL) to check the results.");
    ImGui::EndDisabled();
    ImGui::Spacing();
}
