#pragma once

#include "../Node.hpp"

/**
 * @brief Sink node that drains an AudioStream without writing any output file.
 *
 * Pulls all samples from the upstream pipeline to EOF (triggering any
 * EOF-based sideband writes, e.g. from LoudnessAnalyzerNode), then discards
 * them. ctx.currentFilePath is intentionally left unchanged.
 *
 * Use this as the terminal sink when only analysis or reporting is needed
 * and no output file should be produced.
 *
 * This node is a structural node and is NOT registered in NodeRegistry.
 */
class NullSinkNode : public Node, public StreamSinkNode {
public:
    std::string id()   const override { return "null_sink"; }
    std::string name() const override { return "NullSink"; }
    void init(const std::unordered_map<std::string, std::string>& /*params*/) override {}

    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::None; }

    void consume(std::unique_ptr<AudioStream> stream, NodeContext& ctx) override;
};
