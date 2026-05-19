#include "AnalyzerDispatcher.hpp"

AnalyzerDispatcher::AnalyzerDispatcher(uint32_t parallelThreads)
{
    changeParallelThreads(parallelThreads);
}

AnalyzerDispatcher::~AnalyzerDispatcher()
{
    requestCancelProcessing();
}

void AnalyzerDispatcher::changeParallelThreads(uint32_t newParallelThreads)
{
    // Cancel and destroy existing workers (IWorker dtor joins the thread).
    requestCancelProcessing();
    ebuR128WorkerPool_.clear();
    dcOffsetWorkerPool_.clear();

    parallelThreads_ = newParallelThreads;

    ebuR128WorkerPool_.resize(newParallelThreads);
    for (auto& w : ebuR128WorkerPool_)
        w = std::make_unique<EBUR128Worker>();

    dcOffsetWorkerPool_.resize(newParallelThreads);
    for (auto& w : dcOffsetWorkerPool_)
        w = std::make_unique<DcOffsetWorker>();
}

void AnalyzerDispatcher::addFile(std::shared_ptr<SndFileInfo> file)
{
    const uint32_t dispatchedId = fileCounter_.fetch_add(1, std::memory_order_relaxed) % parallelThreads_;
    ebuR128WorkerPool_[dispatchedId]->addFile(file);
    dcOffsetWorkerPool_[dispatchedId]->addFile(file);
}

void AnalyzerDispatcher::addFiles(SndFileList& fileListFromApp)
{
    for (uint32_t index = 0; index < fileListFromApp.size(); index++)
    {
        const auto& currentFile = fileListFromApp[index];
        const uint32_t dispatchedId = index % parallelThreads_;
        ebuR128WorkerPool_[dispatchedId]->addFile(currentFile);
        dcOffsetWorkerPool_[dispatchedId]->addFile(currentFile);
    }
}

void AnalyzerDispatcher::requestCancelProcessing()
{
    for (auto iter = ebuR128WorkerPool_.begin(); iter != ebuR128WorkerPool_.end(); iter++)
        iter->get()->requestCancelProcessing();
    for (auto iter = dcOffsetWorkerPool_.begin(); iter != dcOffsetWorkerPool_.end(); iter++)
        iter->get()->requestCancelProcessing();
}
