#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

#include "Structures.hpp"

class DcOffsetWorker
{
public:
    DcOffsetWorker() : shouldExit(false), shouldCancelProcessing(false)
    {
        dcOffsetWorkerThread = std::thread(DcOffsetWorker::dcOffsetWorkerMainFunction, this);
    }
    ~DcOffsetWorker()
    {
        {
            std::scoped_lock<std::mutex> lock(pendingFileListMutex);
            shouldExit = true;
        }
        cv.notify_one();
        if (dcOffsetWorkerThread.joinable())
            dcOffsetWorkerThread.join();
    }

    void addFile(std::shared_ptr<SndFileInfo> fileInfoInstance)
    {
        {
            std::scoped_lock<std::mutex> scopedLock(pendingFileListMutex);
            pendingFileList.push(std::move(fileInfoInstance));
        }
        cv.notify_one();
    }

    void requestCancelProcessing(bool request = true)
    {
        shouldCancelProcessing = request;
    }

    static void dcOffsetWorkerMainFunction(DcOffsetWorker *workerInstance);

protected:
    void calcDcOffset(std::shared_ptr<SndFileInfo> fileInfoInstance);

private:
    PendingSndFileQueue pendingFileList;

    std::atomic<bool> shouldExit;   // Request exiting thread
    std::atomic<bool> shouldCancelProcessing;   // Request cancel current processFile() action (invoked by ManifoldApp::onTerminate())
    std::thread dcOffsetWorkerThread;
    std::mutex pendingFileListMutex;
    std::condition_variable cv;
};
