#pragma once

#include "../base_nodes/AnalyzerNode.hpp"
#include "../../base/Report.hpp"

#include <memory>
#include <string>

// --------------------------------------------------------------------------
// MeasurementBackend
// --------------------------------------------------------------------------

/**
 * @brief Selects how LoudnessComplianceNode obtains loudness measurements.
 */
enum class MeasurementBackend {
    /**
     * @brief Read from NodeContext sideband.
     *
     * Reads "loudness_lufs" / "loudness_peak_dbfs" that were written by a
     * prior run's SndFileInfo pre-load (offline) **or** by an upstream
     * LoudnessAnalyzerNode during this run (online). Fast — no extra I/O.
     */
    Sideband,

    /**
     * @brief Feed the audio stream through a live libebur128 pass (inline).
     *
     * Maintains its own ebur128_state; every sample that flows through this
     * node is measured in real time. Reflects the true post-processing signal
     * (e.g. after normalization). No upstream analyzer required.
     */
    InlineEbur128,

    /**
     * @brief Open ctx.sourcePath with libsndfile and run a standalone ebur128 pass.
     *
     * Self-contained — no upstream node required, always measures the
     * original source file regardless of processing applied upstream.
     */
    OfflineEbur128,
};

// --------------------------------------------------------------------------
// LoudnessComplianceReport
// --------------------------------------------------------------------------

/**
 * @brief Per-file loudness compliance evaluation result.
 *
 * Produced by LoudnessComplianceNode::execute() and stored in
 * FileRunRecord::reports.
 */
struct LoudnessComplianceReport : public Report {
    std::string sourcePath;

    double measuredLufs     = 0.0;
    double measuredPeakDbfs = 0.0;
    double targetLufs       = -16.0;
    double tpCeiling        = -1.0;
    double lufsTolerance    = 1.0;

    bool   lufsPass       = false;
    bool   peakPass       = false;
    double lufsDeviation  = 0.0;   ///< measuredLufs - targetLufs (negative = too quiet)
    bool   hasMeasurement = false; ///< false when sideband was absent

    std::string nodeId() const override { return "loudness_compliance"; }
    std::string summary() const override;
    Status status() const override {
        return (hasMeasurement && lufsPass && peakPass) ? Status::Pass : Status::Fail;
    }
};

// --------------------------------------------------------------------------
// LoudnessComplianceNode
// --------------------------------------------------------------------------

/**
 * @brief Atomic node that evaluates EBU R128 loudness compliance for each file.
 *
 * Reads "loudness_lufs" and "loudness_peak_dbfs" from NodeContext sideband
 * (written by an upstream LoudnessAnalyzerNode or pre-loaded by ChainEngine),
 * compares them against the configured target and tolerance, and publishes a
 * LoudnessComplianceReport via ctx.onReport().
 *
 * Must be placed after a LoudnessAnalyzerNode (or after the stream segment
 * when files are pre-analysed).
 */
class LoudnessComplianceNode : public AnalyzerNode {
public:
    LoudnessComplianceNode() = default;

    NODE_ID_AND_NAME_GETTER_DEFINITION;

    void init(const std::unordered_map<std::string, std::string>& params) override;
    std::unordered_map<std::string, std::string> exportConfig() override;

    std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) override;

    void drawUI() override;
    void getUiSize(float& width, float& height) override {
        width  = 0.0f;
        height = 228.0f + 25.0f;
    }

private:
    MeasurementBackend backend_      = MeasurementBackend::Sideband;
    float targetLufs_    = -16.0f;
    float tpCeiling_     =  -1.0f;
    float lufsTolerance_ =   1.0f;
    int   presetIndex_   =   0;    ///< Index into kPresets[]; 0 = Custom
};
