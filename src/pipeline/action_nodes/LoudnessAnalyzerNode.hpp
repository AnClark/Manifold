#pragma once

#include "../base_nodes/AnalyzerNode.hpp"

/**
 * @brief Transparent stream processor that measures EBU R128 integrated loudness.
 *
 * Audio data passes through unmodified. When EOF is detected in read(), the
 * integrated loudness (LUFS-I) and max sample peak (dBFS) are written to
 * the NodeContext sideband under the keys:
 *   "loudness_lufs"       (double)
 *   "loudness_peak_dbfs"  (double)
 *
 * The measurement uses libebur128 and happens on-the-fly during the sink's
 * pull loop — no extra pass over the file is needed.
 *
 * Implemented on top of AnalyzerNode / AnalyzerStream (finalisation happens at
 * EOF), so the engine automatically preserves its finalisation when the output
 * file write is skipped (OutputSinkNode's Skip conflict policy).
 */
class LoudnessAnalyzerNode : public AnalyzerNode {
public:
    std::string id() const override { return "loudness_analyzer"; }
    std::string name() const override { return "LoudnessAnalyzer"; }
    void init(const std::unordered_map<std::string, std::string>& /*params*/) override {}

    std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) override;
};
