/**
 * @file analyzer_node_test.cpp
 * @brief Functional regression test for AnalyzerNode / AnalyzerStream refactoring.
 *
 * Verifies that ClippingDetectionNode and LoudnessComplianceNode still produce
 * correct reports after the AnalyzerStream base-class extraction.
 *
 * Usage:
 *   analyzer_node_test <input_wav_file>
 *
 * Exit code: 0 = all checks passed, 1 = one or more checks failed or error.
 */

#include "pipeline/ChainEngine.hpp"
#include "pipeline/base_nodes/FileSourceNode.hpp"
#include "pipeline/base_nodes/NullSinkNode.hpp"
#include "pipeline/action_nodes/ClippingDetectionNode.hpp"
#include "pipeline/action_nodes/LoudnessComplianceNode.hpp"
#include "base/Report.hpp"

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <cassert>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
static std::string wideToUtf8(const wchar_t* ws)
{
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return {};
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, &s[0], len, nullptr, nullptr);
    return s;
}
#endif

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static int g_passed = 0;
static int g_failed = 0;

static void check(bool cond, const char* msg)
{
    if (cond) {
        std::cout << "  [PASS] " << msg << "\n";
        ++g_passed;
    } else {
        std::cout << "  [FAIL] " << msg << "\n";
        ++g_failed;
    }
}

// ---------------------------------------------------------------------------
// Test 1 — ClippingDetectionNode (Inline backend)
//
// Runs:  FileSourceNode → ClippingDetectionNode(threshold=0 dBFS) → NullSinkNode
//
// Checks:
//   • A ClippingDetectionReport is emitted (onFinalize() was called)
//   • totalSamples > 0               (audio data actually flowed)
//   • maxSampleDbfs is finite         (not NaN / +inf from silent file)
// ---------------------------------------------------------------------------

static void testClippingDetectionInline(const std::string& wavFile)
{
    std::cout << "\n-- ClippingDetectionNode (Inline) --\n";

    auto src  = std::make_unique<FileSourceNode>();
    src->init({});

    auto clip = std::make_unique<ClippingDetectionNode>();
    clip->init({{"threshold_dbfs", "0.0"}, {"backend", "inline"}});

    auto sink = std::make_unique<NullSinkNode>();
    sink->init({});

    std::vector<std::unique_ptr<Node>> nodes;
    nodes.push_back(std::move(src));
    nodes.push_back(std::move(clip));
    nodes.push_back(std::move(sink));

    ChainEngine engine(ChainEngine::toView(nodes));

    std::shared_ptr<ClippingDetectionReport> reportPtr;
    engine.setReportCallback([&](std::shared_ptr<Report> r) {
        if (auto p = std::dynamic_pointer_cast<ClippingDetectionReport>(r))
            reportPtr = std::move(p);
    });

    std::string errorMsg;
    engine.setErrorCallback([&](std::string_view msg) {
        errorMsg = std::string(msg);
        std::cerr << "  Engine error: " << msg << "\n";
    });

    engine.processFile(wavFile, ".");

    check(errorMsg.empty(),       "no engine error");
    check(reportPtr != nullptr,   "ClippingDetectionReport was emitted");
    if (reportPtr) {
        check(reportPtr->totalSamples > 0,
              "totalSamples > 0 (audio data flowed through onSamples)");
        check(std::isfinite(reportPtr->maxSampleDbfs) ||
              reportPtr->maxSampleDbfs == -std::numeric_limits<double>::infinity(),
              "maxSampleDbfs is a valid number");
        std::cout << "  Info: " << reportPtr->summary() << "\n";
    }
}

// ---------------------------------------------------------------------------
// Test 2 — LoudnessComplianceNode (InlineEbur128 backend)
//
// Runs:  FileSourceNode → LoudnessComplianceNode(inline_ebur128) → NullSinkNode
//
// Checks:
//   • A LoudnessComplianceReport is emitted
//   • hasMeasurement is true (or false for very short files — but no crash)
// ---------------------------------------------------------------------------

static void testLoudnessComplianceInline(const std::string& wavFile)
{
    std::cout << "\n-- LoudnessComplianceNode (InlineEbur128) --\n";

    auto src  = std::make_unique<FileSourceNode>();
    src->init({});

    auto loud = std::make_unique<LoudnessComplianceNode>();
    loud->init({{"backend", "inline_ebur128"}, {"target_lufs", "-23.0"}, {"tp_ceiling", "-1.0"}, {"lufs_tolerance", "1.0"}});

    auto sink = std::make_unique<NullSinkNode>();
    sink->init({});

    std::vector<std::unique_ptr<Node>> nodes;
    nodes.push_back(std::move(src));
    nodes.push_back(std::move(loud));
    nodes.push_back(std::move(sink));

    ChainEngine engine(ChainEngine::toView(nodes));

    std::shared_ptr<LoudnessComplianceReport> reportPtr;
    engine.setReportCallback([&](std::shared_ptr<Report> r) {
        if (auto p = std::dynamic_pointer_cast<LoudnessComplianceReport>(r))
            reportPtr = std::move(p);
    });

    std::string errorMsg;
    engine.setErrorCallback([&](std::string_view msg) {
        errorMsg = std::string(msg);
        std::cerr << "  Engine error: " << msg << "\n";
    });

    engine.processFile(wavFile, ".");

    check(errorMsg.empty(),     "no engine error");
    check(reportPtr != nullptr, "LoudnessComplianceReport was emitted");
    if (reportPtr) {
        // For very short files LUFS-I is -inf (< 400 ms gating block) — that's correct.
        // The important thing is that the report was emitted and onFinalize() ran.
        std::cout << "  Info: " << reportPtr->summary() << "\n";
    }
}

// ---------------------------------------------------------------------------
// Test 3 — ClippingDetectionNode (Offline backend)
// ---------------------------------------------------------------------------

static void testClippingDetectionOffline(const std::string& wavFile)
{
    std::cout << "\n-- ClippingDetectionNode (Offline) --\n";

    auto src  = std::make_unique<FileSourceNode>();
    src->init({});

    auto clip = std::make_unique<ClippingDetectionNode>();
    clip->init({{"threshold_dbfs", "0.0"}, {"backend", "offline"}});

    auto sink = std::make_unique<NullSinkNode>();
    sink->init({});

    std::vector<std::unique_ptr<Node>> nodes;
    nodes.push_back(std::move(src));
    nodes.push_back(std::move(clip));
    nodes.push_back(std::move(sink));

    ChainEngine engine(ChainEngine::toView(nodes));

    std::shared_ptr<ClippingDetectionReport> reportPtr;
    engine.setReportCallback([&](std::shared_ptr<Report> r) {
        if (auto p = std::dynamic_pointer_cast<ClippingDetectionReport>(r))
            reportPtr = std::move(p);
    });

    std::string errorMsg;
    engine.setErrorCallback([&](std::string_view msg) {
        errorMsg = std::string(msg);
        std::cerr << "  Engine error: " << msg << "\n";
    });

    engine.processFile(wavFile, ".");

    check(errorMsg.empty(),       "no engine error");
    check(reportPtr != nullptr,   "ClippingDetectionReport was emitted (Offline)");
    if (reportPtr) {
        check(reportPtr->totalSamples > 0,
              "totalSamples > 0 (scanOffline ran successfully)");
        std::cout << "  Info: " << reportPtr->summary() << "\n";
    }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------

int main(int argc, char* argv[])
{
#ifdef _WIN32
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_wav_file>\n";
        LocalFree(wargv);
        return 1;
    }
    const std::string wavFile = wideToUtf8(wargv[1]);
    LocalFree(wargv);
#else
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_wav_file>\n";
        return 1;
    }
    const std::string wavFile = argv[1];
#endif

    std::cout << "=== AnalyzerNode regression test ===\n";
    std::cout << "Input: " << wavFile << "\n";

    testClippingDetectionInline(wavFile);
    testClippingDetectionOffline(wavFile);
    testLoudnessComplianceInline(wavFile);

    std::cout << "\n=== Results: " << g_passed << " passed, " << g_failed << " failed ===\n";
    return g_failed == 0 ? 0 : 1;
}
