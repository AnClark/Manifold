#include "sleep_worker.h"
#include <thread>
using namespace std::chrono_literals;

#include "windows.h"  // For MessageBoxA

void SleepWorker::sleepWorkerMainFuction(SleepWorker *workerInstance)
{
    while (!workerInstance->shouldExit)
    {
        if (workerInstance->shouldDoSleepNow)
        {
            //Sleep(5000);
            std::this_thread::sleep_for(5s);

            MessageBoxA(NULL, "Woke up!", "Test", 0);
            workerInstance->shouldDoSleepNow = false;
        }
        
        // 避免忙等待，释放 CPU 时间片
        std::this_thread::sleep_for(100ms);  // 休眠 100ms，降低循环频率
    }
}
