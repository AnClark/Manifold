#include "SndFileWorker.hpp"
#include <thread>
using namespace std::chrono_literals;


void SndFileWorker::sndFileWorkerMainFuction(SndFileWorker *workerInstance)
{
    while (!workerInstance->shouldExit)
    {
        auto& pendingFileList = workerInstance->pendingFileList;
        while (!pendingFileList.empty())
        {
            SndFileInfo* currentFile = nullptr;

            // 取出待处理文件名（缩小锁范围）
            {
                std::scoped_lock<std::mutex> scopedLock(workerInstance->pendingFileListMutex);
                currentFile = std::move(pendingFileList.front());
                pendingFileList.pop();
            }

            // 在锁外进行文件 I/O 操作
            if (currentFile)
                currentFile->parseSndFile();
        }

        // 避免忙等待，释放 CPU 时间片
        std::this_thread::sleep_for(100ms);  // 休眠 100ms，降低循环频率
    }
}
