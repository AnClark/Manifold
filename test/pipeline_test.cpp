/**
 * @file pipeline_test.cpp
 * @brief End-to-end validation of the Node-based pipeline architecture.
 *
 * Builds the chain:
 *   FileSourceNode → ResamplerNode(48000) → DSPNode(TruePeakLimiter)
 *                  → LoudnessAnalyzerNode → OutputSinkNode(wav) → PrintInfoNode
 *
 * The DSPNode step demonstrates wrapping an IAudioProcessor subclass
 * (TruePeakLimiterProcessor) into the pull pipeline via a factory lambda.
 * In production the measured_true_peak_dbtp value would come from a
 * prior analysis pass; here a conservative 0 dBTP is assumed.
 *
 * Usage:
 *   manifold_pipeline_test <input_audio_file> [output_directory]
 *
 * If output_directory is omitted, files are written to "./pipeline_out/".
 */

#include "pipeline/ChainEngine.hpp"
#include "pipeline/nodes/DSPNode.hpp"
#include "pipeline/nodes/FileSourceNode.hpp"
#include "pipeline/nodes/LoudnessAnalyzerNode.hpp"
#include "pipeline/nodes/OutputSinkNode.hpp"
#include "pipeline/nodes/PrintInfoNode.hpp"
#include "pipeline/nodes/ResamplerNode.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

int main(int argc, char* argv[])
{
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <input_audio_file> [output_directory]\n";
        return 1;
    }

    const std::filesystem::path inputFile  = argv[1];
    const std::filesystem::path outputDir  = argc >= 3 ? argv[2] : "pipeline_out";

    if (!std::filesystem::exists(inputFile)) {
        std::cerr << "Error: input file not found: " << inputFile << "\n";
        return 1;
    }

    // ------------------------------------------------------------------
    // Build the node chain
    // ------------------------------------------------------------------

    // 1. FileSourceNode — opens the input file
    auto fileSource = std::make_unique<FileSourceNode>();
    fileSource->init({});

    // 2. ResamplerNode — target 48 kHz, r8brain backend
    auto resampler = std::make_unique<ResamplerNode>();
    resampler->init({{"rate", "48000"}, {"backend", "r8brain"}});

    // 3. DSPNode — looked up by registry ID, no concrete class include needed.
    //    Any processor added via REGISTER_PROCESSOR can be referenced this way.
    //    In a real batch pipeline the measured_true_peak_dbtp value would
    //    be obtained from a prior analysis pass; 0.0 dBTP (worst case) is
    //    used here so the limiter always applies the full ceiling margin.
    auto truePeakLimiter = DSPNode::fromRegistry("true_peak_limiter", "TruePeakLimiter");
    truePeakLimiter->init({
        {"tp_ceiling",              "-1.0"},   // target ceiling: -1 dBTP
        {"measured_true_peak_dbtp", "0.0"},    // assumed peak: 0 dBTP → 1 dB gain reduction
    });

    // 4. LoudnessAnalyzerNode — EBU R128 integrated loudness (post-limiter)
    auto loudness = std::make_unique<LoudnessAnalyzerNode>();
    loudness->init({});

    // 5. OutputSinkNode — encode to WAV 16-bit
    auto sink = std::make_unique<OutputSinkNode>();
    sink->init({{"format", "wav"}, {"subtype", "pcm16"}});

    // 6. PrintInfoNode — print results to stdout
    auto printer = std::make_unique<PrintInfoNode>();
    printer->init({});

    // ------------------------------------------------------------------
    // Assemble and run
    // ------------------------------------------------------------------

    std::vector<std::unique_ptr<Node>> nodes;
    nodes.push_back(std::move(fileSource));
    nodes.push_back(std::move(resampler));
    nodes.push_back(std::move(truePeakLimiter));
    nodes.push_back(std::move(loudness));
    nodes.push_back(std::move(sink));
    nodes.push_back(std::move(printer));

    ChainEngine engine(std::move(nodes));
    engine.processFile(inputFile, outputDir);

    return 0;
}
