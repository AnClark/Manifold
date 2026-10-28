// processing_dispatch_test.cpp — integration regression test for the
// parallel processing pipeline: ProcessingDispatcher, worker record pairing,
// cancellation, conflict-policy notes, skip-drain behaviour and lazy streams.
#include "dispatchers/ProcessingDispatcher.hpp"
#include "pipeline/base_nodes/AnalyzerNode.hpp"
#include "pipeline/action_nodes/TruePeakLimiterNode.hpp"
#include "pipeline/action_nodes/TrimSilenceNode.hpp"
#include "utils/LogManager.hpp"
#include "TestAudioUtils.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <thread>

namespace fs = std::filesystem;

class PassThroughNode : public Node, public StreamProcessorNode
{
public:
    std::string id() const override { return "test_passthrough"; }
    std::string name() const override { return "Test Pass-Through"; }
    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::AudioStream; }
    void init(const std::unordered_map<std::string, std::string>&) override {}
    std::unique_ptr<AudioStream> wrap(std::unique_ptr<AudioStream> upstream, NodeContext&) override
    { return upstream; }
};

// A minimal analysis node for Phase 8: emits one report when its stream reaches
// EOF — exactly the mechanism the real AnalyzerNode subclasses use, so it
// reproduces the "reports silently dropped on Skip" bug if the sink does not
// drain the stream.
struct TestReport : public Report
{
    std::string nodeId()  const override { return "test_analyzer"; }
    std::string summary() const override { return "test report"; }
};

class TestAnalyzerStream : public AnalyzerStream
{
public:
    using AnalyzerStream::AnalyzerStream;

protected:
    void onFinalize() override
    {
        if (ctx_.onReport)
            ctx_.onReport(std::make_shared<TestReport>());
    }
};

class TestAnalyzerNode : public AnalyzerNode
{
public:
    std::string id() const override { return "test_analyzer"; }
    std::string name() const override { return "Test Analyzer"; }
    void init(const std::unordered_map<std::string, std::string>&) override {}
    std::unique_ptr<AudioStream> wrap(std::unique_ptr<AudioStream> upstream, NodeContext& ctx) override
    {
        return std::make_unique<TestAnalyzerStream>(std::move(upstream), ctx);
    }
};

// Counts the frames actually streamed through it — used by Phase 9 to observe
// whether the Skip path drained the stream or took the no-streaming fast path.
class CountingStream : public AudioStream
{
public:
    CountingStream(std::unique_ptr<AudioStream> upstream, std::atomic<long long>& counter)
        : upstream_(std::move(upstream)), counter_(counter) {}

    size_t read(float* buf, size_t frames) override
    {
        const size_t got = upstream_->read(buf, frames);
        counter_ += static_cast<long long>(got);
        return got;
    }
    const AudioFormat& format() const override { return upstream_->format(); }

private:
    std::unique_ptr<AudioStream> upstream_;
    std::atomic<long long>&      counter_;
};

class CountingNode : public Node, public StreamProcessorNode
{
public:
    std::atomic<long long> framesRead{0};

    std::string id() const override { return "test_counting"; }
    std::string name() const override { return "Test Counting"; }
    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::AudioStream; }
    void init(const std::unordered_map<std::string, std::string>&) override {}
    std::unique_ptr<AudioStream> wrap(std::unique_ptr<AudioStream> upstream, NodeContext&) override
    {
        return std::make_unique<CountingStream>(std::move(upstream), framesRead);
    }
};

static std::shared_ptr<SndFileInfo> makeFileInfo(const std::string& path)
{
    auto fi = std::make_shared<SndFileInfo>();
    fi->parseSndFile(path.c_str());
    fi->isParseOK = true;
    fi->isR128ParsedOK = true;
    fi->isDcOffsetCalculatedOK = true;
    return fi;
}

static bool waitFor(const std::function<bool()>& pred, int timeoutMs = 15000)
{
    for (int waited = 0; waited < timeoutMs; waited += 20)
    {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return pred();
}

static bool outputExists(const std::string& dir, const std::string& wavPath)
{
    const std::string stem = fs::path(wavPath).stem().u8string();
    std::error_code ec;
    const auto p = fs::u8path(dir + "/" + stem + ".wav");
    return fs::exists(p, ec) && fs::file_size(p, ec) > 0;
}

static int failures = 0;
static void check(bool ok, const char* what)
{
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++failures;
}

int main(int argc, char** argv)
{
    // Self-contained: synthesise all fixtures under the scratch directory so
    // the test runs stand-alone under ctest as well as manually.
    const fs::path scratch = TestAudioUtils::scratchDirFromArgs(argc, argv, "manifold_processing_dispatch_test");
    const std::string outDir1 = (scratch / "out1").u8string();
    const std::string outDir2 = (scratch / "out2").u8string();
    std::vector<std::string> wavs;
    for (int i = 1; i <= 7; ++i)
    {
        const std::string wav = (scratch / ("in_" + std::to_string(i) + ".wav")).u8string();
        TestAudioUtils::writeSineWav(wav, 0.5);
        wavs.push_back(wav);
    }
    const std::string emptyWav = (scratch / "empty.wav").u8string();
    TestAudioUtils::writeEmptyWav(emptyWav);

    std::vector<std::shared_ptr<Node>> nodeChain;
    nodeChain.push_back(std::make_shared<PassThroughNode>());
    nodeChain.push_back(std::make_shared<PassThroughNode>());
    std::mutex nodeChainMutex;

    ProcessingDispatcher dispatcher(3);
    dispatcher.setNodeChain(nodeChain, nodeChainMutex);
    dispatcher.setOutputDir(outDir1);
    dispatcher.setOutputFormat(ContainerFormat::Wav, SubtypeOverride::Pcm16);
    dispatcher.setSinkMode(SinkMode::WriteFile);
    dispatcher.setConflictPolicy(FilenameTemplate::ConflictPolicy::Overwrite);

    // ---- Phase 1: ProcessingRun with per-file records (round-robin over 3 workers)
    auto run1 = std::make_shared<ProcessingRun>(1, outDir1, std::vector<std::string>{"TP", "TP"});
    for (const auto& w : wavs)
    {
        auto fi = makeFileInfo(w);
        auto record = std::make_shared<FileRunRecord>(fi);
        record->runCancelToken = &run1->cancelRequested;
        run1->records.push_back(record);
        dispatcher.addFile(fi, record);
    }
    check(waitFor([&] { return run1->isComplete(); }), "phase1: run completes");
    bool allDone = true, allOut = true;
    for (size_t i = 0; i < wavs.size(); ++i)
    {
        allDone &= run1->records[i]->status.load() == FileRunRecord::Status::Done;
        allOut  &= outputExists(outDir1, wavs[i]);
    }
    check(allDone, "phase1: every record is Done");
    check(allOut,  "phase1: every output file written");

    // ---- Phase 2: interleave standalone (no record) and run submissions — stresses record pairing
    dispatcher.setOutputDir(outDir2);
    auto run2 = std::make_shared<ProcessingRun>(2, outDir2, std::vector<std::string>{"TP", "TP"});
    auto fiA = makeFileInfo(wavs[0]);
    dispatcher.addFile(fiA);                                    // standalone, no record
    auto fiB = makeFileInfo(wavs[1 % wavs.size()]);
    auto recB = std::make_shared<FileRunRecord>(fiB);
    recB->runCancelToken = &run2->cancelRequested;
    run2->records.push_back(recB);
    dispatcher.addFile(fiB, recB);                              // run-based
    auto fiC = makeFileInfo(wavs[2 % wavs.size()]);
    dispatcher.addFile(fiC);                                    // standalone, no record
    check(waitFor([&] { return run2->isComplete(); }), "phase2: interleaved run completes");
    check(recB->status.load() == FileRunRecord::Status::Done, "phase2: paired record is Done (not mixed up)");
    check(waitFor([&] { return outputExists(outDir2, wavs[0]) && outputExists(outDir2, wavs[2 % wavs.size()]); }),
          "phase2: standalone outputs written");

    // ---- Phase 3: changeParallelThreads() rebuilds the pool; stored config must be re-applied
    dispatcher.changeParallelThreads(2);
    auto run3 = std::make_shared<ProcessingRun>(3, outDir2, std::vector<std::string>{"TP", "TP"});
    auto fiD = makeFileInfo(wavs[3 % wavs.size()]);
    auto recD = std::make_shared<FileRunRecord>(fiD);
    recD->runCancelToken = &run3->cancelRequested;
    run3->records.push_back(recD);
    dispatcher.addFile(fiD, recD);
    check(waitFor([&] { return run3->isComplete(); }), "phase3: run after resize completes");
    check(recD->status.load() == FileRunRecord::Status::Done, "phase3: config re-applied to new workers");
    check(outputExists(outDir2, wavs[3 % wavs.size()]), "phase3: output written by rebuilt pool");

    // ---- Phase 4: requestCancelProcessing() + resumeProcessing()
    dispatcher.requestCancelProcessing();
    dispatcher.resumeProcessing();
    auto run4 = std::make_shared<ProcessingRun>(4, outDir2, std::vector<std::string>{"TP", "TP"});
    auto fiE = makeFileInfo(wavs[4 % wavs.size()]);
    auto recE = std::make_shared<FileRunRecord>(fiE);
    recE->runCancelToken = &run4->cancelRequested;
    run4->records.push_back(recE);
    dispatcher.addFile(fiE, recE);
    check(waitFor([&] { return run4->isComplete(); }), "phase4: run after cancel/resume completes");
    check(recE->status.load() == FileRunRecord::Status::Done, "phase4: record Done after resume");

    // ---- Phase 5: a file flagged aboutToBeRemoved must not desync the paired record queue.
    // Single worker (strict FIFO) makes any mispairing deterministic: without the
    // onFileSkipped() fix, the skipped file's record is consumed by the NEXT file.
    dispatcher.changeParallelThreads(1);
    auto run5 = std::make_shared<ProcessingRun>(5, outDir2, std::vector<std::string>{"TP", "TP"});
    auto fiF = makeFileInfo(wavs[5 % wavs.size()]);
    fiF->aboutToBeRemoved = true;                       // worker will skip this file
    auto recF = std::make_shared<FileRunRecord>(fiF);
    recF->runCancelToken = &run5->cancelRequested;
    run5->records.push_back(recF);
    dispatcher.addFile(fiF, recF);                      // paired submission
    auto fiG = makeFileInfo(wavs[6 % wavs.size()]);
    auto recG = std::make_shared<FileRunRecord>(fiG);
    recG->runCancelToken = &run5->cancelRequested;
    run5->records.push_back(recG);
    dispatcher.addFile(fiG, recG);
    check(waitFor([&] { return run5->isComplete(); }), "phase5: run with removed file still completes");
    check(recF->status.load() == FileRunRecord::Status::Cancelled,
          "phase5: skipped file's record closed as Cancelled (not stuck Pending)");
    check(recG->status.load() == FileRunRecord::Status::Done,
          "phase5: following record stays correctly paired (not shifted)");

    // ---- Phase 6: run cancel (runCancelToken) closes out queued records.
    // Cancel fires before submission, so every file takes the "run was cancelled"
    // path in processItem() — the same token consumed in parallel mode.
    dispatcher.changeParallelThreads(3);
    auto run6 = std::make_shared<ProcessingRun>(6, outDir2, std::vector<std::string>{"TP", "TP"});
    run6->requestCancel();
    for (int k = 0; k < 4; ++k)
    {
        auto fiK = makeFileInfo(wavs[k % wavs.size()]);
        auto recK = std::make_shared<FileRunRecord>(fiK);
        recK->runCancelToken = &run6->cancelRequested;
        run6->records.push_back(recK);
        dispatcher.addFile(fiK, recK);
    }
    check(waitFor([&] { return run6->isComplete(); }), "phase6: cancelled run reaches completion");
    bool allCancelled = true;
    for (const auto& r : run6->records)
        allCancelled &= r->status.load() == FileRunRecord::Status::Cancelled;
    check(allCancelled, "phase6: every record closed as Cancelled by runCancelToken");

    // ---- Phase 7: conflict-policy outcome hints (FileRunRecord::outputNote).
    // Shown in parentheses in the Tasks "Info" column; only produced when the
    // target file already existed.
    const auto processOne = [&](const std::string& wav, const std::string& outDir,
                                FilenameTemplate::ConflictPolicy policy) -> std::shared_ptr<FileRunRecord>
    {
        std::error_code ec;
        fs::create_directories(fs::u8path(outDir), ec);
        dispatcher.setOutputDir(outDir);
        dispatcher.setConflictPolicy(policy);
        auto fi  = makeFileInfo(wav);
        auto rec = std::make_shared<FileRunRecord>(fi);   // no run — standalone record
        dispatcher.addFile(fi, rec);
        waitFor([&] {
            const auto s = rec->status.load();
            return s != FileRunRecord::Status::Pending && s != FileRunRecord::Status::Processing;
        });
        return rec;
    };
    const auto noteOf = [](const std::shared_ptr<FileRunRecord>& rec) {
        std::scoped_lock<std::mutex> lk(rec->progressMutex);
        return rec->outputNote;
    };

    // Overwrite: first run no note, second run reports the overwrite.
    const std::string d3 = (scratch / "out3").u8string();
    auto r1 = processOne(wavs[0], d3, FilenameTemplate::ConflictPolicy::Overwrite);
    check(r1->status.load() == FileRunRecord::Status::Done && noteOf(r1).empty(),
          "phase7: Overwrite with no conflict -> no note");
    auto r2 = processOne(wavs[0], d3, FilenameTemplate::ConflictPolicy::Overwrite);
    check(r2->status.load() == FileRunRecord::Status::Done && noteOf(r2) == "overwritten",
          "phase7: Overwrite on existing file -> (overwritten)");

    // AutoRename: third run names the output in_2_01, then in_2_02.
    const std::string d4 = (scratch / "out4").u8string();
    auto r3 = processOne(wavs[1], d4, FilenameTemplate::ConflictPolicy::AutoRename);
    check(r3->status.load() == FileRunRecord::Status::Done && noteOf(r3).empty(),
          "phase7: AutoRename with no conflict -> no note");
    auto r4 = processOne(wavs[1], d4, FilenameTemplate::ConflictPolicy::AutoRename);
    check(r4->status.load() == FileRunRecord::Status::Done && noteOf(r4) == "renamed to in_2_01",
          "phase7: AutoRename -> (renamed to in_2_01)");
    auto r5 = processOne(wavs[1], d4, FilenameTemplate::ConflictPolicy::AutoRename);
    check(noteOf(r5) == "renamed to in_2_02", "phase7: AutoRename counter advances -> (renamed to in_2_02)");

    // Skip: second run reports the skip and writes nothing.
    const std::string d5 = (scratch / "out5").u8string();
    auto r6 = processOne(wavs[2], d5, FilenameTemplate::ConflictPolicy::Skip);
    check(r6->status.load() == FileRunRecord::Status::Done && noteOf(r6).empty(),
          "phase7: Skip with no conflict -> no note");
    auto r7 = processOne(wavs[2], d5, FilenameTemplate::ConflictPolicy::Skip);
    check(r7->status.load() == FileRunRecord::Status::Done && noteOf(r7) == "skipped: output exists",
          "phase7: Skip on existing file -> (skipped: output exists)");

    // ---- Phase 8: Skip policy must still drain the stream, so analyzer nodes
    // finalize at EOF and emit their reports even though no file is written.
    {
        std::scoped_lock<std::mutex> cl(nodeChainMutex);
        nodeChain.push_back(std::make_shared<TestAnalyzerNode>());
    }
    const auto reportsOf = [](const std::shared_ptr<FileRunRecord>& rec) {
        std::scoped_lock<std::mutex> lk(rec->progressMutex);
        return rec->reports.size();
    };
    const std::string d6 = (scratch / "out6").u8string();
    auto r8a = processOne(wavs[3], d6, FilenameTemplate::ConflictPolicy::Skip);
    check(r8a->status.load() == FileRunRecord::Status::Done && reportsOf(r8a) == 1,
          "phase8: analyzer report emitted on a normal run");
    auto r8b = processOne(wavs[3], d6, FilenameTemplate::ConflictPolicy::Skip);
    check(r8b->status.load() == FileRunRecord::Status::Done
          && reportsOf(r8b) == 1
          && noteOf(r8b) == "skipped: output exists",
          "phase8: Skip on existing file still emits the analyzer report");

    // ---- Phase 9: Skip fast path. Streams are drained on Skip only when some
    // node finalizes at EOF; pure-processing chains skip processing entirely.
    auto countingNode = std::make_shared<CountingNode>();
    std::vector<std::shared_ptr<Node>> nodeChain9;
    nodeChain9.push_back(countingNode);
    dispatcher.setNodeChain(nodeChain9, nodeChainMutex);
    const std::string d7 = (scratch / "out7").u8string();
    countingNode->framesRead = 0;
    auto r9a = processOne(wavs[4], d7, FilenameTemplate::ConflictPolicy::Skip);
    check(r9a->status.load() == FileRunRecord::Status::Done && countingNode->framesRead > 0,
          "phase9: normal run streams audio through the chain");
    countingNode->framesRead = 0;
    auto r9b = processOne(wavs[4], d7, FilenameTemplate::ConflictPolicy::Skip);
    check(r9b->status.load() == FileRunRecord::Status::Done
          && countingNode->framesRead == 0
          && noteOf(r9b) == "skipped: output exists",
          "phase9: pure-processing chain is not streamed on Skip (fast path)");
    {
        std::scoped_lock<std::mutex> cl(nodeChainMutex);
        nodeChain9.push_back(std::make_shared<TestAnalyzerNode>());
    }
    countingNode->framesRead = 0;
    auto r9c = processOne(wavs[4], d7, FilenameTemplate::ConflictPolicy::Skip);
    check(r9c->status.load() == FileRunRecord::Status::Done
          && countingNode->framesRead > 0
          && reportsOf(r9c) == 1,
          "phase9: chain with a finalizing node is drained on Skip (reports kept)");

    // ---- Phase 10: the drain-on-skip WARN log ("analysis nodes are still
    // running") must fire exactly for the two drained skips (phase8 r8b and
    // phase9 r9c) — not for fast-path skips or normal runs.
    {
        LogFilter f;
        f.minLevel = LogLevel::Warn;
        f.keyword  = "analysis nodes are still running";
        const auto warns = LogManager::instance().filter(f);
        check(warns.size() == 2, "phase10: drain-on-skip WARN logged exactly for the 2 drained skips");
    }

    // ---- Phase 11: eager stream constructors must not run on a fast-path skip.
    // TruePeakLimiterStream buffers + measures its entire upstream; before the
    // lazy-prepare fix that work happened in its constructor (i.e. during
    // wrap()), unconditionally. CountingNode sits upstream of the limiter, so
    // any upstream drain by the limiter shows up as framesRead > 0.
    auto countingNode11 = std::make_shared<CountingNode>();
    std::vector<std::shared_ptr<Node>> nodeChain11;
    nodeChain11.push_back(countingNode11);
    {
        auto tpl = std::make_shared<TruePeakLimiterNode>();
        tpl->init({{"tp_ceiling", "-1.0"}});
        nodeChain11.push_back(tpl);
    }
    dispatcher.setNodeChain(nodeChain11, nodeChainMutex);
    const std::string d8 = (scratch / "out8").u8string();
    countingNode11->framesRead = 0;
    auto r11a = processOne(wavs[5], d8, FilenameTemplate::ConflictPolicy::Skip);
    check(r11a->status.load() == FileRunRecord::Status::Done && countingNode11->framesRead > 0,
          "phase11: normal run streams audio through the limiter");
    countingNode11->framesRead = 0;
    auto r11b = processOne(wavs[5], d8, FilenameTemplate::ConflictPolicy::Skip);
    check(r11b->status.load() == FileRunRecord::Status::Done
          && countingNode11->framesRead == 0
          && noteOf(r11b) == "skipped: output exists",
          "phase11: fast-path skip never triggers the limiter's upstream drain");

    // ---- Phase 12: lazily-prepared streams may throw on the first read()
    // (TrimSilence rejects empty input). The output sink must then close the
    // handle and remove the partial output; the record still ends in Error with
    // the stream's message (same outcome as before the lazy-prepare refactor,
    // when the error was raised at wrap() time before any file was created).
    {
        nodeChain11.clear();
        auto trim = std::make_shared<TrimSilenceNode>();
        trim->init({{"threshold_dbfs", "-60"}, {"padding_ms", "0"}});
        nodeChain11.push_back(trim);
        dispatcher.setNodeChain(nodeChain11, nodeChainMutex);

        const std::string d9 = (scratch / "out9").u8string();
        std::error_code ec;
        fs::create_directories(fs::u8path(d9), ec);
        dispatcher.setOutputDir(d9);
        dispatcher.setConflictPolicy(FilenameTemplate::ConflictPolicy::Overwrite);

        auto fi  = makeFileInfo(emptyWav);  // valid header, 0 frames
        auto rec = std::make_shared<FileRunRecord>(fi);
        dispatcher.addFile(fi, rec);
        waitFor([&] {
            const auto s = rec->status.load();
            return s != FileRunRecord::Status::Pending && s != FileRunRecord::Status::Processing;
        });

        std::string err;
        { std::scoped_lock<std::mutex> lk(rec->progressMutex); err = rec->errorMessage; }
        check(rec->status.load() == FileRunRecord::Status::Error
              && err.find("no frames") != std::string::npos,
              "phase12: empty input ends as Error with the stream's message");
        check(!fs::exists(fs::u8path(d9 + "/empty.wav"), ec),
              "phase12: no partial output file left behind on a mid-pull error");
        LogFilter wf;
        wf.minLevel = LogLevel::Warn;
        wf.keyword  = "discarding the partial output";
        check(LogManager::instance().filter(wf).size() == 1,
              "phase12: cleanup path logged a WARN with the stream's reason");
    }

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}