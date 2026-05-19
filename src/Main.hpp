#pragma once

#include "Application.hpp"
#include "base/SndFileInfo.hpp"
#include "base/ProcessingRun.hpp"
#include "ui_components/Files.hpp"
#include "ui_components/Actions.hpp"
#include "workers/DcOffsetWorker.hpp"
#include "workers/EBUR128Worker.hpp"
#include "workers/SndFileWorker.hpp"
#include "workers/SingleFileProcessorWorker.hpp"
#include "utils/AudioPlayer.hpp"
#include "utils/LogManager.hpp"
#include "pipeline/Node.hpp"
#include "base/IAudioProcessor.hpp"
#include "config/Preferences.hpp"

#include <unordered_set>

enum UIState
{
    pUIFiles,
    pUIActions,
    pUITasks,
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
    void UI_Tasks();
    void UI_Log();

private:
    UIState uiState;

    SndFileList sndFileList;
    std::unordered_set<std::string> sndFilePathSet;  // Normalized path key, used for O(1) duplicate detection
    int detectedDuplicateCount = 0;  // Number of duplicate files detected during the current addition process. Will RESET after showing the warning to avoid stale warnings on next additions.
    int lastClickedIndex;
    std::mutex sndFileListMutex;

    SndFileWorker sndFileWorker;
    EBUR128Worker ebur128Worker;
    DcOffsetWorker dcOffsetWorker;
    SingleFileProcessorWorker singleFileProcessorWorker;

    AudioPlayer audioPlayer;
    std::shared_ptr<SndFileInfo> currentPlayingFile;  // Currently playing file

    std::string NFDLastError;   // TODO: Display error message on UI

    std::vector<std::shared_ptr<Node>> nodeChain;  // Store the current node chain for UI display and management
    std::mutex nodeChainMutex;                      // Protects nodeChain against concurrent access by the worker thread

    std::vector<std::shared_ptr<ProcessingRun>> processingRuns;  ///< History of all runs (oldest first)
    uint64_t nextRunId = 1;

    // UI Components
    UIComponents_Files uiFiles {this};
    UIComponents_Actions uiActions {this};
    friend class UIComponents_Files;
    friend class UIComponents_Actions;

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

    // =============================================================
    // Tasks UI state

    struct TasksUIState
    {
        int selectedRunIdx = -1;  ///< Index into processingRuns; -1 = auto-select latest
    };
    TasksUIState tasksUI;

    // =============================================================
    // Log UI state

    struct LogUIState
    {
        std::vector<LogEntry> snapshot;          // Filtered snapshot of log entries for ImGuiListClipper display (oldest first)
        uint64_t              lastSeq    = 0;    // Last sequence number at the time of the snapshot
        bool                  dirty      = true; // If true, force re-filtering (e.g., when filter conditions change)
        int                   selectedIdx   = -1;
        bool                  autoScroll    = true;
        bool                  scrollToEnd   = false;
        char                  keywordBuf[256] = {};
        int                   minLevelIdx   = 1; // 0=Trace … 5=Fatal
    };
    LogUIState logUI;

    // ==============================================================
    // File name editor UI state

    struct FileNameEditorState
    {
        FilenameTemplate editTemplate;
        int              selectedTokenIdx = -1;
    };
    FileNameEditorState fileNameEditorState;

    // ==============================================================
    // Preferences storage

    PreferencesManager preferences;

    // ==============================================================
    // Output configs - Stored in preference storage for convenience

    std::string     &outputPath             = preferences.outputConfigPref.outputPath;
    ContainerFormat &outputFormat           = preferences.outputConfigPref.outputFormat;
    SubtypeOverride &outputSubtype          = preferences.outputConfigPref.outputSubtype;
    bool            &nullOutput             = preferences.outputConfigPref.nullOutput;  ///< When true, audio is processed but not written to disk
    FilenameTemplate &outputFilenameTemplate = preferences.outputConfigPref.filenameTemplate;

    // ==============================================================
    // Action UI configs

    bool            &exportNodeChainWithOutputConfig = preferences.actionUIPref.exportNodeChainWithOutputConfig;
    bool            &importNodeChainWithOutputConfig = preferences.actionUIPref.importNodeChainWithOutputConfig;
    bool            &rememberRecentOutputConfigPref  = preferences.actionUIPref.rememberRecentOutputConfigPref;
};
