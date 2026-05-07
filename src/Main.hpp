#pragma once

#include "Application.hpp"
#include "base/SndFileInfo.hpp"
#include "workers/DcOffsetWorker.hpp"
#include "workers/EBUR128Worker.hpp"
#include "workers/SndFileWorker.hpp"
#include "workers/SingleFileProcessorWorker.hpp"
#include "utils/AudioPlayer.hpp"
#include "pipeline/Node.hpp"
#include "base/IAudioProcessor.hpp"

#include <unordered_set>

enum UIState
{
    pUIFiles,
    pUIActions,
    pUILog
};

class ManifoldApp : public ImGuiApplication
{
public:
    ManifoldApp() : ImGuiApplication("Manifold"), lastClickedIndex(-1), uiState(pUIFiles)
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
    void UI_Actions();

private:
    UIState uiState;

    SndFileList sndFileList;
    std::unordered_set<std::string> sndFilePathSet;  // 归一化路径 key，用于 O(1) 重复检测
    int detectedDuplicateCount = 0;  // 本次添加过程中检测到的重复文件数量
    int lastClickedIndex;
    std::mutex sndFileListMutex;

    SndFileWorker sndFileWorker;
    EBUR128Worker ebur128Worker;
    DcOffsetWorker dcOffsetWorker;
    SingleFileProcessorWorker singleFileProcessorWorker;

    AudioPlayer audioPlayer;
    std::shared_ptr<SndFileInfo> currentPlayingFile;  // 当前正在播放的文件路径

    std::string NFDLastError;   // TODO: Display error message on UI

    std::vector<std::shared_ptr<Node>> nodeChain;  // Store the current node chain for UI display and management
    std::mutex nodeChainMutex;                      // Protects nodeChain against concurrent access by the worker thread
    std::unordered_map<std::string, std::vector<AudioProcessorParam>> dspNodeParamDefCache;  // Cache for DSP node parameter definitions, key is node name
    std::string outputPath;

    // Node Chain drag/drop (DnD) states & procedures
    struct NodeChainDnDState
    {
        int dragSourceIdx = -1;
        int dropTargetIdx = -1;
        bool isDragging = false;

        void reset()
        {
            dragSourceIdx = -1;
            dropTargetIdx = -1;
            isDragging = false;
        }
    };
    NodeChainDnDState dragDropState;
    void _dragDropIdle(const std::vector<float>& itemTopY, const std::vector<float>& itemBotY);
};
