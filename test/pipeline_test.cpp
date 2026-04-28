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

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
// Convert a UTF-16 wide string to a UTF-8 std::string.
static std::string wideToUtf8(const wchar_t* ws)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, &s[0], len, nullptr, nullptr);
    return s;
}
#endif

// Forward declaration — contains all pipeline logic.
static int run(const std::string& inputFile, const std::string& outputDir);

int main(int argc, char* argv[])
{
    // ---------------------------------------------------------------------------
    // On Windows, argv[] is encoded with the current ANSI code page (e.g. GBK on
    // Chinese Windows), which breaks std::filesystem::u8path() that expects UTF-8.
    // We use GetCommandLineW() + CommandLineToArgvW() to obtain the real UTF-16
    // command-line arguments and convert them to UTF-8 ourselves.
    // This keeps main() as the entry point and works for both console and GUI
    // subsystems (avoiding the wmain / WinMain linker ambiguity with MinGW).
    // On Linux/macOS the locale is UTF-8, so argv[] is already correct.
    // ---------------------------------------------------------------------------
#ifdef _WIN32
    int     wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <input_audio_file> [output_directory]\n";
        LocalFree(wargv);
        return 1;
    }
    const std::string inputFile = wideToUtf8(wargv[1]);
    const std::string outputDir = wargc >= 3 ? wideToUtf8(wargv[2]) : "pipeline_out";
    LocalFree(wargv);
#else
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0]
                  << " <input_audio_file> [output_directory]\n";
        return 1;
    }
    const std::string inputFile = argv[1];
    const std::string outputDir = argc >= 3 ? argv[2] : "pipeline_out";
#endif
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
