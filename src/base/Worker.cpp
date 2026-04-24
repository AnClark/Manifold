#include "Worker.hpp"

IWorker::IWorker() : shouldExit(false), shouldCancelProcessing(false)
{
    workerThread = std::thread(IWorker::workerMainFunction, this);
}

IWorker::~IWorker()
{
    {
        std::scoped_lock<std::mutex> lock(pendingFileListMutex);
        shouldExit = true;
    }
    cv.notify_one();
    if (workerThread.joinable())
        workerThread.join();
}

void IWorker::addFile(std::shared_ptr<SndFileInfo> fileInfoInstance)
{
    {
        std::scoped_lock<std::mutex> scopedLock(pendingFileListMutex);
        pendingFileList.push(std::move(fileInfoInstance));
    }
    cv.notify_one();
}

void IWorker::requestCancelProcessing(bool request)
{
    shouldCancelProcessing = request;
}

void IWorker::workerMainFunction(IWorker* instance)
{
    while (true)
    {
        std::shared_ptr<SndFileInfo> currentFile;

        {
            std::unique_lock<std::mutex> lock(instance->pendingFileListMutex);

            instance->cv.wait(lock, [&] {
                return instance->shouldExit || !instance->pendingFileList.empty();
            });

            if (instance->shouldExit)
                return;

            currentFile = instance->pendingFileList.front();
            instance->pendingFileList.pop();
        }

        if (currentFile
            && !currentFile->aboutToBeRemoved
            && !instance->shouldCancelProcessing
        )
            instance->processItem(currentFile);
    }
}
