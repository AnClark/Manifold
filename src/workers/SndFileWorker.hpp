#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

#include "Structures.hpp"

class SndFileWorker
{
public:
    SndFileWorker() : shouldExit(false)
    {
        // 注意：shouldExit 必须在启动线程前初始化，避免线程读到未定义值
        sndFileWorkerThread = std::thread(SndFileWorker::sndFileWorkerMainFuction, this);
    }
    ~SndFileWorker()
    {
        {
            // 持锁写 shouldExit，保证 cv.wait 的谓词不会错过通知
            std::scoped_lock<std::mutex> lock(pendingFileListMutex);
            shouldExit = true;
        }
        cv.notify_one();  // 立即唤醒线程，无需等待 sleep 超时
        if (sndFileWorkerThread.joinable())
            sndFileWorkerThread.join();
    }

    void addFile(std::shared_ptr<SndFileInfo> fileInfoInstance)
    {
        {
            std::scoped_lock<std::mutex> scopedLock(pendingFileListMutex);
            pendingFileList.push(std::move(fileInfoInstance));
        }
        cv.notify_one();  // 有新任务时唤醒线程
    }

    static void sndFileWorkerMainFuction(SndFileWorker *workerInstance);

private:
    PendingSndFileQueue pendingFileList;

    std::atomic<bool> shouldExit;
    std::thread sndFileWorkerThread;
    std::mutex pendingFileListMutex;
    std::condition_variable cv;  // 用于任务通知和退出唤醒
};
