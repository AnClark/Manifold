#include "sndfile_worker.h"
#include <string>
#include <thread>
using namespace std::chrono_literals;

#include "windows.h"  // For MessageBoxA

void SndFileWorker::sndFileWorkerMainFuction(SndFileWorker *workerInstance)
{
    while (!workerInstance->shouldExit)
    {
        auto& pendingFileList = workerInstance->pendingFileList;
        while (!pendingFileList.empty())
        {
            std::string fileName;
            
            // 取出待处理文件名（缩小锁范围）
            {
                std::scoped_lock<std::mutex> scopedLock(workerInstance->pendingFileListMutex);
                fileName = std::move(pendingFileList.front());
                pendingFileList.pop();
            }

            // 在锁外进行文件 I/O 操作
            SndFileInfo pendingFile;
            pendingFile.filePath = std::string(fileName);
            pendingFile.info = {};  // 先初始化 SF_INFO，以防异常结果

#ifdef _WIN32
            // UTF-8 转 UTF-16 (Windows 宽字符)
            int wlen = MultiByteToWideChar(CP_UTF8, 0, fileName.c_str(), -1, nullptr, 0);
            std::wstring wpath(wlen - 1, 0);
            MultiByteToWideChar(CP_UTF8, 0, fileName.c_str(), -1, &wpath[0], wlen);
            
            pendingFile.handle = sf_wchar_open(wpath.c_str(), SFM_READ, &pendingFile.info);
#else
            pendingFile.handle = sf_open(fileName, SFM_READ, &pendingFile.info);
#endif
            
            if (pendingFile.handle == nullptr)
            {
                // 打开失败，输出错误信息
                const char* errorMsg = sf_error_number(sf_error(nullptr));
                MessageBoxA(nullptr, errorMsg, "sf_open failed", MB_OK | MB_ICONERROR);
                continue;
            }
            
            sf_close(pendingFile.handle);

            // 添加到结果列表（需要线程同步）
            {
                std::scoped_lock<std::mutex> lock(workerInstance->parsedFileListMutex);
                workerInstance->parsedFileList.push_back(pendingFile);
            }
        }
        
        // 避免忙等待，释放 CPU 时间片
        std::this_thread::sleep_for(100ms);  // 休眠 100ms，降低循环频率
    }
}
