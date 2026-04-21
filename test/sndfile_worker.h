#pragma once
#include <mutex>
#include <queue>
#include <thread>
#include <atomic>
#include <string>
#include <vector>
#include <sndfile.h>

struct SndFileInfo
{
    std::string fileName;
    SNDFILE *handle;
    SF_INFO info;
};

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

    void addFile(const char* path)
    {
        std::scoped_lock<std::mutex> scopedLock(pendingFileListMutex);

        pendingFileList.push(std::string(path));
    }

    std::queue<std::string> pendingFileList;    // TODO: 使用列表而不是队列，以应对用户中途取消任务可能导致的错乱
    std::atomic<bool> shouldExit;
    std::thread sndFileWorkerThread;
    std::mutex pendingFileListMutex;

    std::vector<SndFileInfo> parsedFileList;
    std::mutex parsedFileListMutex;  // 保护 parsedFileList 的互斥锁

    static void sndFileWorkerMainFuction(SndFileWorker *workerInstance);
};
