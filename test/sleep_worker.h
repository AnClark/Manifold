#pragma once
#include <thread>
#include <atomic>

class SleepWorker
{
public:
    SleepWorker()
    {
        sleepWorkerThread = std::thread(SleepWorker::sleepWorkerMainFuction, this);
        shouldDoSleepNow = false;
        shouldExit = false;
    }
    ~SleepWorker()
    {
        shouldExit = true;
        if (sleepWorkerThread.joinable())
            sleepWorkerThread.join();
    }

    std::atomic<bool> shouldDoSleepNow;
    std::atomic<bool> shouldExit;
    std::thread sleepWorkerThread;

    static void sleepWorkerMainFuction(SleepWorker *workerInstance);
};
