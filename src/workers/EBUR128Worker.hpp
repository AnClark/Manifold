#pragma once

#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <string>
#include <vector>

#include "Structures.hpp"

class EBUR128Worker
{
public:
    EBUR128Worker()
    {
        ebur128WorkerThread = std::thread(EBUR128Worker::ebur128WorkerMainFuction, this);
        shouldExit = false;
    }
    ~EBUR128Worker()
    {
        shouldExit = true;
        if (ebur128WorkerThread.joinable())
            ebur128WorkerThread.join();
    }

    void addFile(SndFileInfo* fileInfoInstance)
    {
        std::scoped_lock<std::mutex> scopedLock(pendingFileListMutex);

        pendingFileList.push(fileInfoInstance);
    }

    static void ebur128WorkerMainFuction(EBUR128Worker *workerInstance);

protected:
    void processFile(SndFileInfo* fileInfoInstance);

private:
    std::queue<SndFileInfo*> pendingFileList;

    std::atomic<bool> shouldExit;
    std::thread ebur128WorkerThread;
    std::mutex pendingFileListMutex;
};
