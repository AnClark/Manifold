#pragma once

#include "Application.hpp"
#include "Structures.hpp"
#include "workers/SndFileWorker.hpp"

#include <unordered_set>

class ManifoldApp : public ImGuiApplication
{
public:
    ManifoldApp() : ImGuiApplication("Manifold"), lastClickedIndex(-1)
    {}

protected:
    // =============================================================
    // ImGuiApplication interface overrides

    void onInit() override;
    void onTerminate() override;
    void onImGuiDisplay() override;

    // =============================================================
    // UI Components

    void UI_Files();

private:
    SndFileList sndFileList;
    std::unordered_set<std::string> sndFilePathSet;  // 归一化路径 key，用于 O(1) 重复检测
    int detectedDuplicateCount = 0;  // 本次添加过程中检测到的重复文件数量
    int lastClickedIndex;
    std::mutex sndFileListMutex;

    SndFileWorker sndFileWorker;

    std::string NFDLastError;   // TODO: Display error message on UI
};
