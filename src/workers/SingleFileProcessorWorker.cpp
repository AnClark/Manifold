#include "SingleFileProcessorWorker.hpp"
#include "pipeline/ChainEngine.hpp"
#include "utils/LogManager.hpp"

static constexpr const char* LOG_TAG = "Single File Processor";

void SingleFileProcessorWorker::addFile(std::shared_ptr<SndFileInfo> fileInfo,
                                        std::shared_ptr<FileRunRecord> record)
{
    // Push record BEFORE waking the worker so processItem always finds it.
    {
        std::scoped_lock<std::mutex> lock(recordQueueMutex_);
        recordQueue_.push(std::move(record));
    }
    IWorker::addFile(std::move(fileInfo));
}

void SingleFileProcessorWorker::processItem(std::shared_ptr<SndFileInfo> fileInfoInstance)
{
    // Pop the paired FileRunRecord before any early return to keep
    // recordQueue_ in sync with IWorker::pendingFileList.
    std::shared_ptr<FileRunRecord> record;
    {
        std::scoped_lock<std::mutex> lock(recordQueueMutex_);
        if (!recordQueue_.empty())
        {
            record = std::move(recordQueue_.front());
            recordQueue_.pop();
        }
    }

    if (!fileInfoInstance || fileInfoInstance->aboutToBeRemoved || shouldCancelProcessing)
        return;

    if (!nodeChain_ || !nodeChainMutex_)
        return;

    LOG_INFOF(LOG_TAG, "Begin processing file '%s'", fileInfoInstance->filePath.c_str());

    // Guard: reject files whose background analysis is still pending.
    // We distinguish "pending" (flag=false AND error message is empty) from
    // "failed" (flag=false AND error message is non-empty). Failed analyses are
    // allowed through so that nodes can fall back to their passthrough behaviour;
    // only truly pending analyses are intercepted to avoid silently producing
    // incorrect output (e.g. DcOffsetRemoveNode doing nothing, LoudnessNormalize
    // applying a wrong gain).
    {
        const bool parsePending   = !fileInfoInstance->isParseOK
                                    && fileInfoInstance->errorMsg.empty();
        const bool r128Pending    = !fileInfoInstance->isR128ParsedOK
                                    && fileInfoInstance->errorMsgR128.empty();
        const bool dcPending      = !fileInfoInstance->isDcOffsetCalculatedOK
                                    && fileInfoInstance->errorMsgDcOffset.empty();
        if (parsePending || r128Pending || dcPending) {
            const std::string errMsg =
                "File analysis is still in progress (LUFS / DC offset not yet computed). "
                "Please wait for the analysis to finish and try again.";
            if (record) {
                std::scoped_lock<std::mutex> rlock(record->progressMutex);
                record->errorMessage      = errMsg;
                record->timestampFinished = std::chrono::system_clock::now();
                record->status.store(FileRunRecord::Status::Error);
            }
            LOG_ERRORF(LOG_TAG, "File '%s' analysis is still in progress.", fileInfoInstance->filePath.c_str());
            return;
        }
    }

    // Take a snapshot of the node chain under lock.
    // The lock is held only for the duration of the copy (microseconds),
    // so the UI thread is never blocked during actual audio processing.
    // Holding shared_ptr copies keeps every Node alive for the full file
    // even if the UI deletes it from nodeChain in the meantime.
    std::vector<std::shared_ptr<Node>> snapshot;
    {
        std::scoped_lock lock(*nodeChainMutex_);
        if (nodeChain_->empty())
        {
            if (record) {
                std::scoped_lock<std::mutex> rlock(record->progressMutex);
                record->errorMessage      = "Node chain is empty.";
                record->timestampFinished = std::chrono::system_clock::now();
                record->status.store(FileRunRecord::Status::Error);
            }
            return;
        }
        snapshot = *nodeChain_;
    }

    if (record) {
        record->timestampStarted = std::chrono::system_clock::now();
        record->status.store(FileRunRecord::Status::Processing);
    }

    // Create a local copy of outputDir under lock
    std::string outputDir_Copied;
    {
        std::scoped_lock<std::mutex> lock(outputDirMutex);
        outputDir_Copied = outputDir_;
    }

    // Build a non-owning view from the snapshot with sourceNode_ implicitly prepended
    std::vector<Node*> localView;
    localView.reserve(2 + snapshot.size());
    localView.push_back(&sourceNode_);
    for (auto& n : snapshot)
        localView.push_back(n.get());
    
    outputNode_.init({{"format", "wav"}, {"subtype", "pcm16"}});
    localView.push_back(&outputNode_);

    // Construct the engine and set up callbacks for progress and error reporting
    bool hadError = false;
    ChainEngine engine(std::move(localView));
    engine.setProgressCallback([&](size_t idx, std::string_view name) {
        const std::string nameStr(name);
        LOG_TRACEF(LOG_TAG, "Processing file '%s': now at node %zu (%s)",
                   fileInfoInstance->filePath.c_str(), idx, nameStr.c_str());
        if (record) {
            std::scoped_lock<std::mutex> rlock(record->progressMutex);
            record->currentNodeIndex = idx;
            record->currentNodeName  = nameStr;
        }
    });
    engine.setErrorCallback([&](std::string_view msg) {
        hadError = true;
        if (record) {
            std::scoped_lock<std::mutex> rlock(record->progressMutex);
            record->errorMessage = std::string(msg);
        }
        LOG_ERRORF(LOG_TAG, "File '%s' processing error: %s", fileInfoInstance->filePath.c_str(), msg.data());
    });

    // Now everything is set up, let's go!
    engine.processFile(*fileInfoInstance, outputDir_Copied);

    if (record) {
        record->timestampFinished = std::chrono::system_clock::now();
        record->status.store(hadError ? FileRunRecord::Status::Error
                                      : FileRunRecord::Status::Done);
    }

    LOG_INFOF(LOG_TAG, "Finished processing file '%s'", fileInfoInstance->filePath.c_str());
}
