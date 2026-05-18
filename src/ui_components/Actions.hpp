#pragma once

#include "config/FilenameConfig.hpp"

// Forward decls.
class ManifoldApp;

class UIComponents_Actions
{
public:
    UIComponents_Actions(ManifoldApp* app_) : app(app_)
    {}

    void subUI_NodeChainView();

    static constexpr float toolChainBtnWidth  = 25.0f + 4.0f;
    static constexpr float toolChainBtnHeight = 25.0f;
    void button_ExportNodeChainToFile();
    void button_ImportNodeChainFromFile();
    void button_OpenNodeChainMenu();
    void popup_NodeChainMenu();

    void command_StartProcessingAllFiles();

    void button_SelectOutputFolder();

    void button_FileName();
    void popup_OutputFileNameRule(FilenameTemplate& s_editTemplate, int& s_selectedTokenIdx);

    void combo_SelectContainerFormat();
    void combo_SelectSampleSubtype();
    void toggle_EnableNullOutput();
    void button_ProcessAllFiles();

    bool query_CanProcess() const;
    const char* query_ProcessingHints() const;
    void info_ShowProcessingHints();

    void subUI_ShowLatestRunStatus();

private:
    ManifoldApp* app;
};
