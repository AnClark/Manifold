#pragma once

#include "base/Worker.hpp"
#include "base/ProcessingRun.hpp"
#include "base/AudioFormats.hpp"
#include "pipeline/Node.hpp"
#include "pipeline/base_nodes/FileSourceNode.hpp"
#include "pipeline/base_nodes/OutputSinkNode.hpp"
#include "pipeline/base_nodes/NullSinkNode.hpp"

using namespace AudioFormats;

/** @brief Controls what happens at the end of the audio pipeline. */
enum class SinkMode {
    WriteFile,  ///< Encode and write to outputDir (default)
    Null,       ///< Drain stream and discard — no file written
};

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

    void setOutputFormat(ContainerFormat format, SubtypeOverride subtype)
    {
        std::scoped_lock<std::mutex> lock(outputFormatMutex);
        this->outputFormat_ = format;
        this->outputSubType_ = subtype;
    }

    void setSinkMode(SinkMode mode)
    {
        std::scoped_lock<std::mutex> lock(outputFormatMutex);
        sinkMode_ = mode;
    }

protected:
    void processItem(std::shared_ptr<SndFileInfo> fileInfoInstance) override;

private:
    std::vector<std::shared_ptr<Node>>* nodeChain_ = nullptr;
    std::mutex*                          nodeChainMutex_ = nullptr;
    std::string outputDir_;

    ContainerFormat    outputFormat_  = ContainerFormat::Wav;
    SubtypeOverride    outputSubType_ = SubtypeOverride::Pcm16;
    SinkMode           sinkMode_      = SinkMode::WriteFile;

    FileSourceNode sourceNode_;   ///< Implicitly prepended to the user-supplied chain
    OutputSinkNode outputNode_;   ///< Used when sinkMode_ == WriteFile
    NullSinkNode   nullSinkNode_; ///< Used when sinkMode_ == Null

    std::mutex outputDirMutex;
    std::mutex outputFormatMutex;

    // Paired record queue — one entry per IWorker::pendingFileList entry.
    // nullptr entries correspond to standalone (non-run) submissions.
    std::queue<std::shared_ptr<FileRunRecord>> recordQueue_;
    std::mutex                                 recordQueueMutex_;
};
