#pragma once

#include "../base_nodes/AnalyzerNode.hpp"
#include "../../base/Report.hpp"

#include <cstdint>
#include <memory>
#include <string>

// --------------------------------------------------------------------------
// ClippingBackend
// --------------------------------------------------------------------------

/**
 * @brief Selects how ClippingDetectionNode scans for clipped samples.
 */
enum class ClippingBackend {
    /**
     * @brief Scan the live audio stream as it passes through this node.
     *
     * Every sample that flows through is inspected in real time. Reflects the
     * true post-processing signal — detects clipping introduced by any
     * upstream node (e.g. after LoudnessNormalize). No extra I/O.
     */
    Inline,

    /**
     * @brief Open ctx.sourcePath with libsndfile and scan independently.
     *
     * Self-contained — always inspects the original source file regardless of
     * where the node sits in the chain or what upstream nodes have applied.
     */
    Offline,
};

// --------------------------------------------------------------------------
// ClippingDetectionReport
// --------------------------------------------------------------------------

/**
 * @brief Per-file clipping scan result.
 *
 * Produced by ClippingDetectionNode and stored in FileRunRecord::reports.
 * pass/fail is determined by whether clippedSamples <= maxAllowedClipped.
 */
struct ClippingDetectionReport : public Report {
    std::string sourcePath;

    int64_t  totalSamples       = 0;   ///< Total samples examined (all channels)
    int64_t  clippedSamples     = 0;   ///< Samples whose absolute value >= threshold
    double   clippingRatePpm    = 0.0; ///< clippedSamples / totalSamples × 1 000 000
    double   maxSampleDbfs      = 0.0; ///< Highest absolute sample value seen, in dBFS
    uint32_t clippedChannelMask = 0;   ///< Bit i set when channel i had ≥1 clipped sample

    float   thresholdDbfs     = 0.0f; ///< Threshold used during this scan (stored for display)
    int64_t maxAllowedClipped = 0;    ///< Tolerance configured at scan time

    std::string nodeId()  const override { return "clipping_detection"; }
    std::string summary() const override;
    std::string details() const override;
    Status status() const override {
        return clippedSamples <= maxAllowedClipped ? Status::Pass : Status::Fail;
    }
};

// --------------------------------------------------------------------------
// ClippingDetectionNode
// --------------------------------------------------------------------------

/**
 * @brief Transparent stream processor that scans for digitally clipped samples.
 *
 * Audio data passes through unmodified. The node counts every sample whose
 * absolute value meets or exceeds a configurable dBFS threshold (default 0.0,
 * i.e. full scale) and publishes a ClippingDetectionReport via ctx.onReport()
 * at the end of each file.
 *
 * Two backends are available:
 *   - Inline  — inspects the live post-processing stream (default).
 *   - Offline — opens ctx.sourcePath with libsndfile for an independent pass.
 *
 * Sideband written:
 *   "clipping_count" (int64_t) — total clipped samples, available to any
 *   downstream node that wishes to act on this result.
 */
class ClippingDetectionNode : public AnalyzerNode {
public:
    ClippingDetectionNode() = default;

    NODE_ID_AND_NAME_GETTER_DEFINITION;

    void init(const std::unordered_map<std::string, std::string>& params) override;
    std::unordered_map<std::string, std::string> exportConfig() override;

    std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) override;

    void drawUI() override;
    void getUiSize(float& width, float& height) override
    {
        width  = 0.0f;
        height = 160.0f + 32.0f;
    }

private:
    ClippingBackend backend_          = ClippingBackend::Inline;
    float           thresholdDbfs_    = 0.0f;
    int             maxAllowedClipped_ = 0;    ///< Stored as int for ImGui InputInt; cast to int64_t at use
};
