#include "ProcessingDispatcher.hpp"

ProcessingDispatcher::ProcessingDispatcher(uint32_t processingThreads)
{
    changeParallelThreads(processingThreads);
}

ProcessingDispatcher::~ProcessingDispatcher()
{
    requestCancelProcessing();
}

void ProcessingDispatcher::changeParallelThreads(uint32_t newProcessingThreads)
{
    // Cancel and destroy existing workers (IWorker dtor joins the thread).
    requestCancelProcessing();
    workerPool_.clear();

    threads_ = newProcessingThreads;

    workerPool_.resize(newProcessingThreads);
    for (auto& w : workerPool_)
        w = std::make_unique<SingleFileProcessorWorker>();

    // Re-apply the stored configuration to the freshly created workers.
    std::scoped_lock<std::mutex> lock(configMutex_);
    for (auto& w : workerPool_)
        applyConfigTo(*w);
}

void ProcessingDispatcher::applyConfigTo(SingleFileProcessorWorker& worker)
{
    // NOTE: configMutex_ must be held by the caller.
    if (nodeChain_ && nodeChainMutex_)
        worker.setNodeChain(*nodeChain_, *nodeChainMutex_);
    worker.setOutputDir(outputDir_);
    worker.setOutputFormat(outputFormat_, outputSubType_);
    worker.setSinkMode(sinkMode_);
    worker.setConflictPolicy(conflictPolicy_);
}

void ProcessingDispatcher::setNodeChain(std::vector<std::shared_ptr<Node>>& nodeChain, std::mutex& mutex)
{
    std::scoped_lock<std::mutex> lock(configMutex_);
    nodeChain_      = &nodeChain;
    nodeChainMutex_ = &mutex;
    for (auto& w : workerPool_)
        w->setNodeChain(nodeChain, mutex);
}

void ProcessingDispatcher::setOutputDir(std::string outputDir)
{
    std::scoped_lock<std::mutex> lock(configMutex_);
    outputDir_ = std::move(outputDir);
    for (auto& w : workerPool_)
        w->setOutputDir(outputDir_);
}

void ProcessingDispatcher::setOutputFormat(ContainerFormat format, SubtypeOverride subtype)
{
    std::scoped_lock<std::mutex> lock(configMutex_);
    outputFormat_  = format;
    outputSubType_ = subtype;
    for (auto& w : workerPool_)
        w->setOutputFormat(format, subtype);
}

void ProcessingDispatcher::setSinkMode(SinkMode mode)
{
    std::scoped_lock<std::mutex> lock(configMutex_);
    sinkMode_ = mode;
    for (auto& w : workerPool_)
        w->setSinkMode(mode);
}

void ProcessingDispatcher::setConflictPolicy(FilenameTemplate::ConflictPolicy policy)
{
    std::scoped_lock<std::mutex> lock(configMutex_);
    conflictPolicy_ = policy;
    for (auto& w : workerPool_)
        w->setConflictPolicy(policy);
}

void ProcessingDispatcher::addFile(std::shared_ptr<SndFileInfo> file)
{
    const auto fileId = fileCounter_.fetch_add(1, std::memory_order_relaxed);
    const uint32_t dispatchedId = fileId % threads_;
    workerPool_[dispatchedId]->addFile(std::move(file));
}

void ProcessingDispatcher::addFile(std::shared_ptr<SndFileInfo> file, std::shared_ptr<FileRunRecord> record)
{
    // The file and its record travel paired into the same worker queue, so
    // SingleFileProcessorWorker::processItem() always finds the right record.
    const auto fileId = fileCounter_.fetch_add(1, std::memory_order_relaxed);
    const uint32_t dispatchedId = fileId % threads_;
    workerPool_[dispatchedId]->addFile(std::move(file), std::move(record));
}

void ProcessingDispatcher::addFiles(SndFileList& fileListFromApp)
{
    for (uint32_t index = 0; index < fileListFromApp.size(); index++)
    {
        const uint32_t dispatchedId = index % threads_;
        workerPool_[dispatchedId]->addFile(fileListFromApp[index]);
    }
}

void ProcessingDispatcher::requestCancelProcessing()
{
    for (auto& w : workerPool_)
        w->requestCancelProcessing();
}

void ProcessingDispatcher::resumeProcessing()
{
    for (auto& w : workerPool_)
        w->requestCancelProcessing(false);
}