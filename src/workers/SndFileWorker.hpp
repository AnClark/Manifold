#pragma once

#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <string>
#include <vector>

#include "Structures.hpp"

class SndFileWorker
{
public:
    SndFileWorker()
    {
        sndFileWorkerThread = std::thread(SndFileWorker::sndFileWorkerMainFuction, this);
        shouldExit = false;
    }
    ~SndFileWorker()
    {
        shouldExit = true;
        if (sndFileWorkerThread.joinable())
            sndFileWorkerThread.join();
    }

    void addFile(SndFileInfo* fileInfoInstance)
    {
        std::scoped_lock<std::mutex> scopedLock(pendingFileListMutex);

        pendingFileList.push(fileInfoInstance);
    }

    static void sndFileWorkerMainFuction(SndFileWorker *workerInstance);

private:
    std::queue<SndFileInfo*> pendingFileList;

    std::atomic<bool> shouldExit;
    std::thread sndFileWorkerThread;
    std::mutex pendingFileListMutex;
};
