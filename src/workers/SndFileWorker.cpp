#include "SndFileWorker.hpp"


void SndFileWorker::sndFileWorkerMainFuction(SndFileWorker *workerInstance)
{
    while (true)
    {
        std::shared_ptr<SndFileInfo> currentFile;

        {
            std::unique_lock<std::mutex> lock(workerInstance->pendingFileListMutex);

            // 阻塞等待，直到有新任务或收到退出信号
            // 使用谓词避免虚假唤醒 (spurious wakeup)
            workerInstance->cv.wait(lock, [&] {
                return workerInstance->shouldExit || !workerInstance->pendingFileList.empty();
            });

            // 收到退出信号：不再处理剩余队列，立即返回
            if (workerInstance->shouldExit)
                return;

            currentFile = workerInstance->pendingFileList.front();
            workerInstance->pendingFileList.pop();
        }

        // 在锁外进行文件 I/O 操作，若已被标记取消则跳过
        if (currentFile && !currentFile->cancelled)
            currentFile->parseSndFile();
    }
}
