#pragma once

#include "../Node.hpp"

#include <ebur128.h>

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
 */
class LoudnessAnalyzerNode : public Node, public StreamProcessorNode {
public:
    std::string id() const override { return "loudness_analyzer"; }
    std::string name() const override { return "LoudnessAnalyzer"; }
    void init(const std::unordered_map<std::string, std::string>& /*params*/) override {}

    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::AudioStream; }

    std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) override;
};
