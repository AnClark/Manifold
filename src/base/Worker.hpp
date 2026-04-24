#pragma once

#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>

#include "Structures.hpp"

// 所有 Worker 的抽象基类
// 封装了：任务队列、线程生命周期、取消机制
// 子类只需实现 processItem() 来完成实际业务逻辑
class IWorker
{
public:
    IWorker();
    virtual ~IWorker();

    // 向任务队列提交一个待处理文件
    void addFile(std::shared_ptr<SndFileInfo> fileInfoInstance);

    // 请求取消当前及后续处理（程序退出时调用）
    void requestCancelProcessing(bool request = true);

protected:
    // 子类实现具体的文件处理逻辑
    virtual void processItem(std::shared_ptr<SndFileInfo> fileInfoInstance) = 0;

    std::atomic<bool> shouldCancelProcessing{false};

private:
    static void workerMainFunction(IWorker* instance);

    PendingSndFileQueue pendingFileList;
    std::atomic<bool> shouldExit{false};
    std::thread workerThread;
    std::mutex pendingFileListMutex;
    std::condition_variable cv;
};
