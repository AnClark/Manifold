#include "LoudnessComplianceNode.hpp"
#include "pipeline/NodeRegistry.hpp"
#include "utils/LogManager.hpp"
#include "utils/SfOpenUtf8.hpp"

#include <ebur128.h>
#include <sndfile.h>
#include <imgui.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>
#include <string>

// --------------------------------------------------------------------------
// Node registration
// --------------------------------------------------------------------------

REGISTER_NODE(
    LoudnessComplianceNode,
    "loudness_compliance",
    "Loudness Compliance",
    "Evaluates whether each file's integrated loudness and true peak meet a target standard.",
    "Analysis",
    NodeRole::StreamProcessor);

// --------------------------------------------------------------------------
// LoudnessComplianceReport::summary() / details()
// --------------------------------------------------------------------------

std::string LoudnessComplianceReport::summary() const
{
    if (!hasMeasurement) return "\xe2\x80\x94"; // em dash
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f LUFS", measuredLufs);
    return buf;
}

std::string LoudnessComplianceReport::details() const
{
    if (!hasMeasurement)
        return "No measurement available \xe2\x80\x94 ensure the correct backend is selected.\n"
               "Sideband requires an upstream LoudnessAnalyzerNode or pre-analyzed files.";

    char buf[160];
    std::snprintf(buf, sizeof(buf),
        "LUFS: %.1f  (target %.1f \xc2\xb1%.1f)  [%s]\nTP: %.1f dBFS  [%s]",
        measuredLufs,
        targetLufs, lufsTolerance, lufsPass ? "PASS" : "FAIL",
        measuredPeakDbfs,         peakPass ? "PASS" : "FAIL");
    return buf;
}

// --------------------------------------------------------------------------
// Preset table
// --------------------------------------------------------------------------

namespace {

struct CompliancePreset {
    const char* name;
    float targetLufs;
    float tpCeiling;
    float lufsTolerance;
    const char* note;  ///< Short source/context note shown in UI tooltip
};

// Loudness standards references:
//   EBU R128        : EBU Tech 3341 (2020)
//   Apple Music     : Apple Music Loudness Specification
//   Steam           : Steam Audio Guidelines (upper-limit recommendation)
//   Wwise           : Audiokinetic recommended default (EBU R128 aligned)
//   FMOD            : FMOD Studio internal loudness bus default reference
//   Sony PSN        : PlayStation TRC audio requirements (PS4/PS5, ITU-R BS.1770-4)
//   Xbox / XGS      : Xbox GDK audio guidelines (ITU-R BS.1770-4)
//   Nintendo Switch : Nintendo Lot Check audio requirements (ITU-R BS.1770-4)
static constexpr CompliancePreset kPresets[] = {
    { "Custom",           0.0f,  0.0f, 0.0f, "" },
    { "EBU R128",       -23.0f, -1.0f, 1.0f, "EBU Tech 3341 (2020)" },
    { "Apple Music",    -16.0f, -1.0f, 1.0f, "Apple Music Loudness Specification" },
    { "Steam",          -14.0f, -1.0f, 2.0f, "Steam audio guidelines (upper-limit recommendation)" },
    { "Wwise Default",  -23.0f, -1.0f, 1.0f, "Audiokinetic recommended default (EBU R128 aligned)" },
    { "FMOD Default",   -18.0f, -1.0f, 2.0f, "FMOD Studio internal loudness bus reference" },
    { "Sony PSN",       -24.0f, -1.0f, 2.0f, "PlayStation TRC audio requirements (PS4/PS5, ITU-R BS.1770-4)" },
    { "Xbox / XGS",     -24.0f, -2.0f, 2.0f, "Xbox GDK audio guidelines (ITU-R BS.1770-4)" },
    { "Nintendo Switch",-24.0f, -1.0f, 1.0f, "Nintendo Lot Check audio requirements (ITU-R BS.1770-4)" },
};

static constexpr int kNumPresets = static_cast<int>(sizeof(kPresets) / sizeof(kPresets[0]));

static constexpr const char* kBackendNames[] = {
    "Sideband",
    "Inline ebur128",
    "Offline ebur128",
};
static constexpr int kNumBackends = 3;

} // anonymous namespace

// --------------------------------------------------------------------------
// init / exportConfig
// --------------------------------------------------------------------------

void LoudnessComplianceNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto tryGetFloat = [&](const std::string& key, float& out) {
        auto it = params.find(key);
        if (it != params.end()) {
            try { out = std::stof(it->second); } catch (...) {}
        }
    };
    tryGetFloat("target_lufs",    targetLufs_);
    tryGetFloat("tp_ceiling",     tpCeiling_);
    tryGetFloat("lufs_tolerance", lufsTolerance_);

    auto it = params.find("backend");
    if (it != params.end()) {
        if      (it->second == "inline_ebur128")  backend_ = MeasurementBackend::InlineEbur128;
        else if (it->second == "offline_ebur128") backend_ = MeasurementBackend::OfflineEbur128;
        else                                      backend_ = MeasurementBackend::Sideband;
    }
}

std::unordered_map<std::string, std::string> LoudnessComplianceNode::exportConfig()
{
    static constexpr const char* kBackendKeys[] = {
        "sideband", "inline_ebur128", "offline_ebur128"
    };
    return {
        { "target_lufs",    std::to_string(targetLufs_)    },
        { "tp_ceiling",     std::to_string(tpCeiling_)     },
        { "lufs_tolerance", std::to_string(lufsTolerance_) },
        { "backend",        kBackendKeys[static_cast<int>(backend_)] },
    };
}

// --------------------------------------------------------------------------
// LoudnessComplianceStream
// --------------------------------------------------------------------------

namespace {

class LoudnessComplianceStream : public AnalyzerStream {
public:
    LoudnessComplianceStream(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx,
        MeasurementBackend backend,
        float targetLufs, float tpCeiling, float lufsTolerance)
        : AnalyzerStream(std::move(upstream), ctx)
        , backend_(backend)
        , targetLufs_(targetLufs)
        , tpCeiling_(tpCeiling)
        , lufsTolerance_(lufsTolerance)
    {
        if (backend_ == MeasurementBackend::InlineEbur128) {
            const AudioFormat& fmt = upstream_->format();
            ebur128_ = ebur128_init(
                static_cast<unsigned int>(fmt.channels),
                static_cast<unsigned long>(fmt.sampleRate),
                EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK);
            if (!ebur128_) {
                LOG_ERRORF("LoudnessCompliance",
                    "ebur128_init failed — cannot proceed with InlineEbur128 measurement.");
                throw std::runtime_error(
                    "[LoudnessCompliance] ebur128_init failed for InlineEbur128 mode.");
            }
        }
    }

    ~LoudnessComplianceStream() override
    {
        if (ebur128_)
            ebur128_destroy(&ebur128_);
    }

protected:
    void onSamples(const float* buf, size_t frames) override
    {
        if (backend_ == MeasurementBackend::InlineEbur128 && ebur128_)
            ebur128_add_frames_float(ebur128_, buf, frames);
    }

private:
    // ------------------------------------------------------------------
    // Helpers
    // ------------------------------------------------------------------

    /** Evaluate and populate a report from already-known lufs/peak values. */
    void fillReport(LoudnessComplianceReport& r, double lufs, double peakDbfs) const
    {
        r.hasMeasurement   = true;
        r.measuredLufs     = lufs;
        r.measuredPeakDbfs = peakDbfs;
        r.lufsDeviation    = lufs - static_cast<double>(targetLufs_);

        const double lo = static_cast<double>(targetLufs_) - static_cast<double>(lufsTolerance_);
        const double hi = static_cast<double>(targetLufs_) + static_cast<double>(lufsTolerance_);
        r.lufsPass = (lufs >= lo && lufs <= hi);
        r.peakPass = (peakDbfs <= static_cast<double>(tpCeiling_));
    }

    /** Run a full libsndfile + ebur128 pass on filePath, return lufs/peak via out-params.
     *  Returns false when opening or measuring fails. */
    static void measureOffline(const std::string& filePath,
                               double& outLufs, double& outPeakDbfs)
    {
        SF_INFO sfInfo = {};
        SNDFILE* file = SfOpenUtf8(filePath, SFM_READ, &sfInfo);
        if (!file) {
            const std::string sfErr = sf_strerror(nullptr);
            LOG_ERRORF("LoudnessCompliance",
                "OfflineEbur128: cannot open '%s': %s",
                filePath.c_str(), sfErr.c_str());
            throw std::runtime_error(
                "[LoudnessCompliance] OfflineEbur128: cannot open '" + filePath + "': " + sfErr);
        }

        ebur128_state* st = ebur128_init(
            static_cast<unsigned int>(sfInfo.channels),
            static_cast<unsigned long>(sfInfo.samplerate),
            EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK);
        if (!st) {
            LOG_ERRORF("LoudnessCompliance",
                "OfflineEbur128: ebur128_init failed for '%s'.", filePath.c_str());
            sf_close(file);
            throw std::runtime_error(
                "[LoudnessCompliance] OfflineEbur128: ebur128_init failed for '" + filePath + "'.");
        }

        const size_t chunkFrames = static_cast<size_t>(sfInfo.samplerate); // 1 s
        auto* buf = static_cast<double*>(
            std::malloc(chunkFrames
                        * static_cast<size_t>(sfInfo.channels)
                        * sizeof(double)));
        if (!buf) {
            LOG_ERRORF("LoudnessCompliance", "OfflineEbur128: out of memory.");
            ebur128_destroy(&st);
            sf_close(file);
            throw std::runtime_error("[LoudnessCompliance] OfflineEbur128: out of memory.");
        }

        sf_count_t n;
        while ((n = sf_readf_double(file, buf,
                    static_cast<sf_count_t>(chunkFrames))) > 0)
            ebur128_add_frames_double(st, buf, static_cast<size_t>(n));

        std::free(buf);
        sf_close(file);

        if (ebur128_loudness_global(st, &outLufs) != EBUR128_SUCCESS) {
            ebur128_destroy(&st);
            throw std::runtime_error(
                "[LoudnessCompliance] OfflineEbur128: ebur128_loudness_global failed for '" + filePath + "'.");
        }

        double maxPeak = 0.0;
        for (unsigned int ch = 0;
             ch < static_cast<unsigned int>(sfInfo.channels); ++ch)
        {
            double p = 0.0;
            if (ebur128_sample_peak(st, ch, &p) == EBUR128_SUCCESS)
                maxPeak = std::max(maxPeak, p);
        }
        outPeakDbfs = maxPeak > 0.0 ? 20.0 * std::log10(maxPeak)
                                    : -std::numeric_limits<double>::infinity();

        ebur128_destroy(&st);
    }

    // ------------------------------------------------------------------
    // finalize
    // ------------------------------------------------------------------

    void onFinalize() override
    {
        // For Sideband / OfflineEbur128 this is the first moment we have access
        // to a complete sideband (any upstream analyzer's own finalize() ran just
        // before upstream returned 0 to AnalyzerStream::read()).

        auto report = std::make_shared<LoudnessComplianceReport>();
        report->sourcePath    = ctx_.sourcePath;
        report->targetLufs    = targetLufs_;
        report->tpCeiling     = tpCeiling_;
        report->lufsTolerance = lufsTolerance_;

        switch (backend_)
        {
        case MeasurementBackend::InlineEbur128:
        {
            if (ebur128_) {
                double lufs = -std::numeric_limits<double>::infinity();
                double maxPeak = 0.0;
                if (ebur128_loudness_global(ebur128_, &lufs) == EBUR128_SUCCESS) {
                    const int ch = upstream_->format().channels;
                    for (int c = 0; c < ch; ++c) {
                        double p = 0.0;
                        if (ebur128_sample_peak(ebur128_,
                                static_cast<unsigned int>(c), &p) == EBUR128_SUCCESS)
                            maxPeak = std::max(maxPeak, p);
                    }
                    const double peakDbfs = maxPeak > 0.0
                        ? 20.0 * std::log10(maxPeak)
                        : -std::numeric_limits<double>::infinity();
                    fillReport(*report, lufs, peakDbfs);
                }
            }
            break;
        }
        case MeasurementBackend::OfflineEbur128:
        {
            if (ctx_.sourcePath.empty()) {
                LOG_ERRORF("LoudnessCompliance",
                    "OfflineEbur128: ctx.sourcePath is empty — cannot measure.");
                throw std::runtime_error(
                    "[LoudnessCompliance] OfflineEbur128: source path is empty.");
            }
            double lufs = 0.0, peakDbfs = 0.0;
            measureOffline(ctx_.sourcePath, lufs, peakDbfs); // throws on failure
            fillReport(*report, lufs, peakDbfs);
            break;
        }
        case MeasurementBackend::Sideband:
        default:
        {
            auto lufs = ctx_.getSideband<double>("loudness_lufs");
            auto peak = ctx_.getSideband<double>("loudness_peak_dbfs");
            if (lufs.has_value() && peak.has_value()) {
                fillReport(*report, *lufs, *peak);
            } else {
                LOG_WARNF("LoudnessCompliance",
                    "File '%s': loudness sideband not available — "
                    "re-analyze audio file in Files view, or use Inline / Offline ebur128 instead.",
                    ctx_.sourcePath.c_str());
            }
            break;
        }
        }

        if (ctx_.onReport)
            ctx_.onReport(std::move(report));
    }

    MeasurementBackend backend_;
    float targetLufs_;
    float tpCeiling_;
    float lufsTolerance_;
    ebur128_state* ebur128_ = nullptr;
};

} // anonymous namespace

// --------------------------------------------------------------------------
// wrap
// --------------------------------------------------------------------------

std::unique_ptr<AudioStream> LoudnessComplianceNode::wrap(
    std::unique_ptr<AudioStream> upstream,
    NodeContext& ctx)
{
    return std::make_unique<LoudnessComplianceStream>(
        std::move(upstream), ctx,
        backend_, targetLufs_, tpCeiling_, lufsTolerance_);
}

// --------------------------------------------------------------------------
// drawUI
// --------------------------------------------------------------------------

void LoudnessComplianceNode::drawUI()
{
    const float labelWidth = ImGui::CalcTextSize("True Peak Ceiling").x + 20.0f;

    // Backend selector
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Backend");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    int backendIdx = static_cast<int>(backend_);
    if (ImGui::Combo("##backend", &backendIdx, kBackendNames, kNumBackends))
        backend_ = static_cast<MeasurementBackend>(backendIdx);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay) && ImGui::BeginTooltip()) {
        ImGui::Text("Instruction about the backends:");
        ImGui::BulletText("Sideband        — read pre-analyzed values from Files view (no upstream node needed).");
        ImGui::BulletText("Inline ebur128  — measure the live post-processing stream in real time.");
        ImGui::BulletText("Offline ebur128 — directly open audio file and run a standalone libebur128 pass.");
        ImGui::Separator();
        ImGui::Text("Notice:");
        ImGui::BulletText("\"Sideband\" and \"Offline ebur128\" only measures the original file, whose results are not affected by any other nodes.");
        ImGui::BulletText("If you wish to measure audio stream processed by other nodes (e.g. check if Loudness Normalize Node works well), \n"
                          "please use \"Inline ebur128\" instead.");

        ImGui::EndTooltip();
    }

    // Preset selector
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Preset");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::BeginCombo("##preset", kPresets[presetIndex_].name)) {
        for (int i = 0; i < kNumPresets; i++) {
            const bool selected = (presetIndex_ == i);
            if (ImGui::Selectable(kPresets[i].name, selected)) {
                presetIndex_ = i;
                if (i != 0) {
                    targetLufs_    = kPresets[i].targetLufs;
                    tpCeiling_     = kPresets[i].tpCeiling;
                    lufsTolerance_ = kPresets[i].lufsTolerance;
                }
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
            // Show source note as tooltip
            if (kPresets[i].note[0] != '\0' && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", kPresets[i].note);
        }
        ImGui::EndCombo();
    }

    // Target LUFS
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Target Loudness");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##target_lufs", &targetLufs_, -36.0f, -6.0f, "%.1f LUFS"))
        presetIndex_ = 0;

    // LUFS Tolerance
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("LUFS Tolerance");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##lufs_tolerance", &lufsTolerance_, 0.0f, 4.0f, "+/- %.1f LU"))
        presetIndex_ = 0;

    // TP Ceiling
    ImGui::Spacing();
    ImGui::AlignTextToFramePadding();
    ImGui::Text("True Peak Ceiling");
    ImGui::SameLine(labelWidth);
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderFloat("##tp_ceiling", &tpCeiling_, -9.0f, 0.0f, "%.1f dBTP"))
        presetIndex_ = 0;

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    ImGui::BeginDisabled();
    ImGui::TextWrapped("Results stored in Task records.\nOpen Tasks view, hover on the badge (which shows PASS or FAIL) to check the results.");
    ImGui::EndDisabled();
}


