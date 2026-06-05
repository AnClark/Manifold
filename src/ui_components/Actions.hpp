#pragma once

#include "config/FilenameConfig.hpp"
#include "config/WorkflowManager.hpp"

// Forward decls.
class ManifoldApp;

class UIComponents_Actions
{
public:
    UIComponents_Actions(ManifoldApp* app_) : app(app_)
    {}

    void subUI_ActionList();
    void subUI_WorkflowList();
    void subroutine_LoadWorkflow(const WorkflowDescriptor& w);
    void subroutine_RevealWorkflow(const WorkflowDescriptor& w);

    void popup_SaveWorkflow();
    void popup_ConfirmDeleteWorkflow();
    void popup_RenameWorkflow();
    void popup_ChangeGroupWorkflow();
    void popup_ConfirmOverwriteWorkflow();

    void subUI_NodeChainView();

    static constexpr float toolChainBtnWidth  = 25.0f + 4.0f;
    static constexpr float toolChainBtnHeight = 25.0f;
    void button_CollapseAllNodes(int collapsedCount);
    void button_ExpandAllNodes(int uncollapsedCount);
    void button_ExportNodeChainToFile();
    void button_ImportNodeChainFromFile(bool runAsCommand = false);
    void button_OpenNodeChainMenu();
    void popup_NodeChainMenu();
    void subroutine_ImportNodeChain(std::string path);
    void popup_ConfirmLoadNodeChain();

    void command_StartProcessingAllFiles();

    void button_SelectOutputFolder();
    void popup_ConfirmSetOutputFolder();    

    void button_FileName();
    void popup_OutputFileNameRule(FilenameTemplate& s_editTemplate, int& s_selectedTokenIdx);

    void combo_SelectContainerFormat();
    void combo_SelectSampleSubtype();
    void toggle_EnableNullOutput();
    void button_ProcessAllFiles();

    bool query_CanProcess() const;
    const char* query_ProcessingHints() const;
    bool query_AllFilesFullyAnalyzed() const;
    void info_ShowProcessingHints();

    void subUI_ShowLatestRunStatus();

    void system_DropHandler(int count, const char** paths);
    enum class PendingDialog { Null, LoadNodeChainConfirm, SetOutputFolderConfirm,
                               SaveWorkflow, DeleteWorkflowConfirm,
                               RenameWorkflow, ChangeGroupWorkflow, OverwriteWorkflowConfirm };
    PendingDialog pendingDialog { PendingDialog::Null };

private:
    ManifoldApp* app;

    std::string dndReceivedPath;

    // ── Workflow editor state ─────────────────────────────────────────────
    struct SaveWorkflowState
    {
        char nameBuf[256]  = {};
        char descBuf[512]  = {};
        char groupBuf[128] = {};
        bool includeOutputConfig = false;
    } saveWfState;

    WorkflowDescriptor pendingDeleteWorkflow;   ///< Workflow staged for deletion confirmation
    WorkflowDescriptor pendingRenameWorkflow;   ///< Workflow staged for rename
    WorkflowDescriptor pendingChangeGrpWorkflow;///< Workflow staged for group change
    WorkflowDescriptor pendingOverwriteWorkflow;///< Workflow staged for overwrite-with-current

    struct RenameWorkflowState
    {
        char nameBuf[256] = {};
    } renameWfState;

    struct ChangeGroupState
    {
        char groupBuf[128] = {};
    } changeGrpState;
};
