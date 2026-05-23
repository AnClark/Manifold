#pragma once

#include "Report.hpp"
#include "SndFileInfo.hpp"
#include "pipeline/NodeRegistry.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <set>
#include <vector>

using RunTimePoint = std::chrono::system_clock::time_point;

// ---------------------------------------------------------------------------
// FileRunRecord — per-file result record within a single ProcessingRun.
//
// Written by the worker thread (status, progress, error);
// read by the UI thread for display.
// ---------------------------------------------------------------------------
struct FileRunRecord
{
    enum class Status : uint8_t { Pending, Processing, Done, Error, Cancelled };

    explicit FileRunRecord(std::shared_ptr<SndFileInfo> fi)
        : fileInfo(std::move(fi)) {}

    // Non-copyable (atomic + mutex members)
    FileRunRecord(const FileRunRecord&)            = delete;
    FileRunRecord& operator=(const FileRunRecord&) = delete;

    std::shared_ptr<SndFileInfo> fileInfo;

    std::atomic<Status> status { Status::Pending };

    /// Pre-resolved output filename stem (without extension).
    /// Set by the UI thread at run creation from the active FilenameTemplate.
    /// Empty string = fall back to the source file's own stem.
    std::string resolvedOutputStem;

    /// Points to the owning ProcessingRun::cancelRequested flag.
    /// Set by the UI thread at run creation; read by the worker thread to
    /// interrupt stream processing mid-file and skip queued files.
    const std::atomic<bool>* runCancelToken = nullptr;

    // Protected by progressMutex — written by worker, read by UI
    size_t      currentNodeIndex { 0 };
    std::string currentNodeName;
    std::string errorMessage;   ///< Non-empty only when status == Error
    std::vector<std::shared_ptr<Report>> reports;  ///< Collected during processing; guarded by progressMutex
    mutable std::mutex progressMutex;

    void addReport(std::shared_ptr<Report> r) {
        std::lock_guard<std::mutex> lock(progressMutex);
        reports.push_back(std::move(r));
    }

    RunTimePoint timestampStarted;
    RunTimePoint timestampFinished;
};

// ---------------------------------------------------------------------------
// ProcessingRun — one batch-processing invocation snapshot.
//
// Created on the UI thread when the user clicks "Process All Files".
// Owns a snapshot of the file list and per-file result records; all results
// are isolated from subsequent runs even if the same files are re-submitted.
// ---------------------------------------------------------------------------
struct ProcessingRun
{
    ProcessingRun(uint64_t runId,
                  std::string outDir,
                  std::vector<std::string> nodeNames)
        : id(runId)
        , outputDir(std::move(outDir))
        , nodeChainSnapshot(std::move(nodeNames))
    {
        timestampCreated = std::chrono::system_clock::now();
    }

    uint64_t     id;
    RunTimePoint timestampCreated;

    std::string              outputDir;
    std::vector<std::string> nodeChainSnapshot;  ///< Node names captured at run creation

    std::vector<std::shared_ptr<FileRunRecord>> records;

    std::set<std::string> reportSrcsPopulated;
    std::vector<std::string_view> reportSrcIds;
    std::vector<std::string_view> reportSrcNames;

    /// Set from the UI thread (Cancel button); read by worker thread via FileRunRecord::runCancelToken.
    std::atomic<bool> cancelRequested{false};

    void requestCancel() { cancelRequested.store(true, std::memory_order_relaxed); }

    // ---- Derived queries (UI thread only) ---------------------------------

    size_t countByStatus(FileRunRecord::Status s) const
    {
        size_t n = 0;
        for (const auto& r : records)
            if (r->status.load(std::memory_order_relaxed) == s) ++n;
        return n;
    }

    bool isComplete() const
    {
        if (records.empty()) return false;
        for (const auto& r : records)
        {
            auto s = r->status.load(std::memory_order_relaxed);
            if (s == FileRunRecord::Status::Pending ||
                s == FileRunRecord::Status::Processing)
                return false;
        }
        return true;
    }

    float getProgress() const
    {
        if (records.empty()) return 1.0f;
        const size_t done = countByStatus(FileRunRecord::Status::Done)
                          + countByStatus(FileRunRecord::Status::Error)
                          + countByStatus(FileRunRecord::Status::Cancelled);
        return static_cast<float>(done) / static_cast<float>(records.size());
    }

    void populateReportSources()
    {
        // Avoid executing too repeatedly
        if (isComplete() && !reportSrcsPopulated.empty())
            return;

        // Find all report sources (nodeIds)
        // Store new entries in a std::set to eliminate duplicated entries
        for (const auto& rec : records)
            for (const auto& report : rec->reports)
                reportSrcsPopulated.insert(report->nodeId());
        
        // Then, copy all entries to reportSrcIds for quick, internal access on UI side.
        // At the same time, get their display names and store in reportSrcNames.
        // NOTE: Only update if reportSrcsPopulated changed.
        if (reportSrcsPopulated.size() != reportSrcIds.size())
        {
            // Cleanup first
            reportSrcIds.clear();
            reportSrcNames.clear();

            const NodeRegistry& reg = NodeRegistry::getInstance();
            for (const auto& id : reportSrcsPopulated)
            {
                reportSrcIds.push_back(id);

                const auto nodeEntry = reg.findById(id);
                if (nodeEntry)
                    reportSrcNames.push_back(nodeEntry->displayName);
                else
                    reportSrcNames.push_back(id);
            }            
        }



    }
};
