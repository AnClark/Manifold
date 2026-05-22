#pragma once

#include "../base_nodes/AnalyzerNode.hpp"
#include "../../base/Report.hpp"

#include <string>
#include <vector>

// --------------------------------------------------------------------------
// DynamicRangeReport
// --------------------------------------------------------------------------

/**
 * @brief Dynamic range measurement result for one audio file.
 *
 * Produced by DynamicRangeNode; follows the TT Dynamic Range Meter algorithm
 * (Pleasurize Music Foundation, dr.loudness-war.info):
 *
 *   1. Divide the signal into fixed-length blocks (default: 3 seconds).
 *   2. Compute per-channel RMS for each block.
 *   3. Sort block RMS values descending; average the top 20% (≥ 2 blocks).
 *   4. DR_ch = floor( 20 × log₁₀(peak_ch / avg_top_rms_ch) )
 *   5. DR    = floor( mean(DR_ch) )
 *
 * The Crest Factor is also provided as a complementary metric:
 *   Crest_ch = 20 × log₁₀(peak_ch / overall_rms_ch)
 *
 * Both metrics indicate how compressed the material is — low values suggest
 * heavy limiting or clipping.
 */
struct DynamicRangeReport : public Report {
    std::string sourcePath;

    double drValue        = 0.0; ///< Overall DR (floor of per-channel average, e.g. 12 for "DR12")
    double crestFactorDb  = 0.0; ///< Crest Factor = 20×log₁₀(peak/overall_rms), dB
    int    blocksAnalyzed = 0;   ///< Number of 3-second blocks included in the DR calculation

    std::vector<double> perChannelDr;          ///< Pre-floor DR per channel
    std::vector<double> perChannelCrestFactor; ///< Crest Factor per channel, dB
    std::vector<double> perChannelPeakDbfs;    ///< Absolute sample peak per channel, dBFS

    /** @brief Returns false when the file was too short to produce a result (<1 block). */
    bool hasResult() const { return blocksAnalyzed > 0; }

    std::string nodeId()  const override { return "dynamic_range"; }
    std::string summary() const override;
    Status status() const override { return Status::Info; }

    // passed() uses the base-class default (true) — DR is an informational metric,
    // not a binary pass/fail gate.  Use ClippingDetectionNode for hard QA gates.
};

// --------------------------------------------------------------------------
// DynamicRangeNode
// --------------------------------------------------------------------------

/**
 * @brief Transparent stream decorator that measures TT Dynamic Range (DR)
 *        and Crest Factor for each audio file.
 *
 * Audio data is passed through completely unmodified.
 *
 * ## Algorithm — TT DR Meter (Pleasurize Music Foundation)
 *
 *   1. Split audio into 3-second blocks.
 *   2. Compute per-channel RMS for each block, and track the global sample peak.
 *   3. Sort block RMS values descending; average the loudest 20% (≥ 2 blocks).
 *   4. DR_ch = floor( 20 × log₁₀( peak_ch / avg_top_rms_ch ) )
 *   5. DR    = floor( mean(DR_ch) )
 *
 * Results are compatible with the fre:ac, Picard, and Quod Libet DR plug-ins.
 *
 * ## Crest Factor
 *
 *   Crest = 20 × log₁₀(peak / overall_rms)
 *
 * Higher values indicate more dynamic material; aggressively compressed or
 * limited audio typically shows Crest < 10 dB.
 *
 * ## Sidebands written
 *
 *   "dr_value"        (double) — DR integer value (e.g. 12.0 for DR12)
 *   "crest_factor_db" (double) — Crest Factor in dB
 *
 * These sidebands are available to any downstream node after this one runs.
 */
class DynamicRangeNode : public AnalyzerNode {
public:
    DynamicRangeNode() = default;

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
        height = 148.0f + 56.0f;
    }

private:
    /// Duration of each analysis block in seconds.  The TT DR standard uses 3 s.
    /// Exposed as a parameter for testing; normal use should keep the default.
    float blockDurationSec_ = 3.0f;
};
