#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

#include "Structures.hpp"

class EBUR128Worker
{
public:
    EBUR128Worker() : shouldExit(false)
    {
        ebur128WorkerThread = std::thread(EBUR128Worker::ebur128WorkerMainFuction, this);
    }
    ~EBUR128Worker()
    {
        {
            std::scoped_lock<std::mutex> lock(pendingFileListMutex);
            shouldExit = true;
        }
        cv.notify_one();
        if (ebur128WorkerThread.joinable())
            ebur128WorkerThread.join();
    }

    void addFile(std::shared_ptr<SndFileInfo> fileInfoInstance)
    {
        {
            std::scoped_lock<std::mutex> scopedLock(pendingFileListMutex);
            pendingFileList.push(std::move(fileInfoInstance));
        }
        cv.notify_one();
    }

    static void ebur128WorkerMainFuction(EBUR128Worker *workerInstance);

protected:
    void processFile(std::shared_ptr<SndFileInfo> fileInfoInstance);

private:
    PendingSndFileQueue pendingFileList;

    std::atomic<bool> shouldExit;
    std::thread ebur128WorkerThread;
    std::mutex pendingFileListMutex;
    std::condition_variable cv;
};
