/**
 * @file pipeline_test.cpp
 * @brief End-to-end validation of the Node-based pipeline architecture.
 *
 * Builds the chain:
 *   FileSourceNode → ResamplerNode(48000) → TruePeakLimiterNode
 *                  → LoudnessAnalyzerNode → OutputSinkNode(wav) → PrintInfoNode
 *
 * Usage:
 *   manifold_pipeline_test [scratch_dir]
 *
 * The input fixture is generated and the outputs are written under the
 * scratch directory (see test/TestAudioUtils.hpp).
 */

#include "pipeline/ChainEngine.hpp"
#include "pipeline/base_nodes/FileSourceNode.hpp"
#include "pipeline/action_nodes/LoudnessAnalyzerNode.hpp"
#include "pipeline/base_nodes/OutputSinkNode.hpp"
#include "pipeline/action_nodes/PrintInfoNode.hpp"
#include "pipeline/base_nodes/ResamplerNode.hpp"
#include "pipeline/action_nodes/TruePeakLimiterNode.hpp"
#include "TestAudioUtils.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>

// Forward declaration — contains all pipeline logic.
static int run(const std::string& inputFile, const std::string& outputDir);

int main(int argc, char* argv[])
{
    // Self-contained: synthesises its fixture and writes output under the
    // scratch directory so it runs stand-alone under ctest as well as manually.
    const auto scratch        = TestAudioUtils::scratchDirFromArgs(argc, argv, "manifold_pipeline_test");
    const std::string inputFile = (scratch / "input.wav").u8string();
    const std::string outputDir = (scratch / "pipeline_out").u8string();
    TestAudioUtils::writeSineWav(inputFile, 1.0);
    std::cout << "Fixture: " << inputFile << "\n";
    return run(inputFile, outputDir);
}

static int run(const std::string& inputFile, const std::string& outputDir)
{
    // inputFile / outputDir are UTF-8 on all platforms at this point.
    if (!std::filesystem::exists(std::filesystem::u8path(inputFile))) {
        std::cerr << "Error: input file not found: " << inputFile << "\n";
        return 1;
    }

    // ------------------------------------------------------------------
    // Build the node chain
    // ------------------------------------------------------------------

    // 1. FileSourceNode — opens the input file
    auto fileSource = std::make_unique<FileSourceNode>();
    fileSource->init({});

    // 2. ResamplerNode — target is customizable, r8brain backend
    auto resampler = std::make_unique<ResamplerNode>();
    resampler->init({{"rate", "192000"}, {"backend", "r8brain"}});

    // 3. TruePeakLimiterNode — limits True Peak to ceiling.
    //    In a real batch pipeline, true_peak_dbtp in the sideband would be
    //    populated by an upstream analysis node; here we fall back to
    //    static init() params (0.0 dBTP assumed → 1 dB gain reduction).
    auto truePeakLimiter = std::make_unique<TruePeakLimiterNode>();
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

    ChainEngine engine(ChainEngine::toView(nodes));
    engine.processFile(inputFile, outputDir);

    return 0;
}
