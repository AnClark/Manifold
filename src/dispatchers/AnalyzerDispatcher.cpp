#include "AnalyzerDispatcher.hpp"

AnalyzerDispatcher::AnalyzerDispatcher(uint32_t ebuR128Threads, uint32_t dcOffsetThreads)
{
    changeParallelThreads(ebuR128Threads, dcOffsetThreads);
}

AnalyzerDispatcher::~AnalyzerDispatcher()
{
    requestCancelProcessing();
}

void AnalyzerDispatcher::changeParallelThreads(uint32_t newEbuR128Threads, uint32_t newDcOffsetThreads)
{
    // Cancel and destroy existing workers (IWorker dtor joins the thread).
    requestCancelProcessing();
    ebuR128WorkerPool_.clear();
    dcOffsetWorkerPool_.clear();

    ebuR128Threads_ = newEbuR128Threads;
    dcOffsetThreads_ = newDcOffsetThreads;

    ebuR128WorkerPool_.resize(newEbuR128Threads);
    for (auto& w : ebuR128WorkerPool_)
        w = std::make_unique<EBUR128Worker>();

    dcOffsetWorkerPool_.resize(newDcOffsetThreads);
    for (auto& w : dcOffsetWorkerPool_)
        w = std::make_unique<DcOffsetWorker>();
}

void AnalyzerDispatcher::addFile(std::shared_ptr<SndFileInfo> file)
{
    const auto fileId = fileCounter_.fetch_add(1, std::memory_order_relaxed);
    const uint32_t ebuR128DispatchedId = fileId % ebuR128Threads_;
    const uint32_t dcOffsetDispatchedId = fileId % dcOffsetThreads_;
    ebuR128WorkerPool_[ebuR128DispatchedId]->addFile(file);
    dcOffsetWorkerPool_[dcOffsetDispatchedId]->addFile(file);
}

void AnalyzerDispatcher::addFiles(SndFileList& fileListFromApp)
{
    for (uint32_t index = 0; index < fileListFromApp.size(); index++)
    {
        const auto& currentFile = fileListFromApp[index];
        const uint32_t ebuR128DispatchedId = index % ebuR128Threads_;
        const uint32_t dcOffsetDispatchedId = index % dcOffsetThreads_;
        ebuR128WorkerPool_[ebuR128DispatchedId]->addFile(currentFile);
        dcOffsetWorkerPool_[dcOffsetDispatchedId]->addFile(currentFile);
    }
}

void AnalyzerDispatcher::requestCancelProcessing()
{
    for (auto iter = ebuR128WorkerPool_.begin(); iter != ebuR128WorkerPool_.end(); iter++)
        iter->get()->requestCancelProcessing();
    for (auto iter = dcOffsetWorkerPool_.begin(); iter != dcOffsetWorkerPool_.end(); iter++)
        iter->get()->requestCancelProcessing();
}
