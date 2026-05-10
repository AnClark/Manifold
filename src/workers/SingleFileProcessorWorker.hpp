#pragma once

#include "base/Worker.hpp"
#include "base/ProcessingRun.hpp"
#include "pipeline/Node.hpp"
#include "pipeline/base_nodes/FileSourceNode.hpp"
#include "pipeline/base_nodes/OutputSinkNode.hpp"

class SingleFileProcessorWorker : public IWorker
{
public:
    void setNodeChain(std::vector<std::shared_ptr<Node>>& nodeChain, std::mutex& mutex)
    {
        nodeChain_ = &nodeChain;
        nodeChainMutex_ = &mutex;
    }

    // Overrides IWorker::addFile to keep recordQueue_ in sync.
    // Submits the file without a run record (legacy / standalone use).
    void addFile(std::shared_ptr<SndFileInfo> fileInfo)
    {
        {
            std::scoped_lock<std::mutex> lock(recordQueueMutex_);
            recordQueue_.push(nullptr);  // null = standalone submission
        }
        IWorker::addFile(std::move(fileInfo));
    }

    // Submits a file as part of a ProcessingRun.
    // The record will be driven through its lifecycle by the worker thread.
    void addFile(std::shared_ptr<SndFileInfo> fileInfo, std::shared_ptr<FileRunRecord> record);

    void setOutputDir(std::string outputDir) 
    {
        std::scoped_lock<std::mutex> lock(outputDirMutex);
        this->outputDir_ = std::move(outputDir);
    }

protected:
    void processItem(std::shared_ptr<SndFileInfo> fileInfoInstance) override;

private:
    std::vector<std::shared_ptr<Node>>* nodeChain_ = nullptr;
    std::mutex*                          nodeChainMutex_ = nullptr;
    std::string outputDir_;

    FileSourceNode sourceNode_;  ///< Implicitly prepended to the user-supplied chain
    OutputSinkNode outputNode_;  ///< Implicitly prepended to the user-supplied chain as well

    std::mutex outputDirMutex;

    // Paired record queue — one entry per IWorker::pendingFileList entry.
    // nullptr entries correspond to standalone (non-run) submissions.
    std::queue<std::shared_ptr<FileRunRecord>> recordQueue_;
    std::mutex                                 recordQueueMutex_;
};
