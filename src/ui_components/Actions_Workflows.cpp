#include "Actions.hpp"
#include "Main.hpp"
#include "config/Config.hpp"

#include <imgui.h>
#include "ImGuiNotify_MOD.hpp"
#include "../fonts/IconFontAwesome5_Unique.h"

#include <cstring>
#include <filesystem>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

// ─────────────────────────────────────────────────────────────────────────────
// Save-as-Workflow popup
// ─────────────────────────────────────────────────────────────────────────────

void UIComponents_Actions::popup_SaveWorkflow()
{
    const auto closePopup = [this]()
    {
        pendingDialog = PendingDialog::Null;
        ImGui::CloseCurrentPopup();
    };

    ImGui::SetNextWindowSize(ImVec2(440, 0), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("##save_workflow", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SeparatorText("Save as Workflow");
        ImGui::Spacing();

        // ── Name (required) ───────────────────────────────────────────────
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Name *");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##wf_name", saveWfState.nameBuf, sizeof(saveWfState.nameBuf));

        const bool nameEmpty = (saveWfState.nameBuf[0] == '\0');
        if (nameEmpty)
        {
            ImGui::SameLine(0, 6);
            ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "(required)");
        }

        // Live overwrite warning (check every frame — cheap linear scan of in-memory list)
        const std::string currentGroup = saveWfState.groupBuf;
        const bool willOverwrite = !nameEmpty &&
            app->WorkflowManager.nameExistsInGroup(saveWfState.nameBuf, currentGroup);
        if (willOverwrite)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f),
                ICON_FA_EXCLAMATION_TRIANGLE " A workflow with this name already exists in this group.");
            ImGui::TextColored(ImVec4(1.f, 0.8f, 0.2f, 1.f),
                "  Saving will overwrite it.");
        }
        ImGui::Spacing();

        // ── Group ─────────────────────────────────────────────────────────
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Group");

        // InputText fills the row minus a small combo-button on the right
        constexpr float kPickBtnWidth = 26.0f;
        ImGui::SetNextItemWidth(-kPickBtnWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##wf_group", saveWfState.groupBuf, sizeof(saveWfState.groupBuf));

        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetNextItemWidth(kPickBtnWidth);
        if (ImGui::BeginCombo("##wf_group_pick", "", ImGuiComboFlags_NoPreview))
        {
            const auto existingGroups = app->WorkflowManager.listGroups();
            if (existingGroups.empty())
                ImGui::TextDisabled("(no groups yet)");
            for (const auto& g : existingGroups)
            {
                if (ImGui::Selectable(g.c_str()))
                {
                    std::strncpy(saveWfState.groupBuf, g.c_str(), sizeof(saveWfState.groupBuf) - 1);
                    saveWfState.groupBuf[sizeof(saveWfState.groupBuf) - 1] = '\0';
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Pick from existing groups");
        if (saveWfState.groupBuf[0] == '\0')
            ImGui::TextDisabled("  Leave empty to save without a group");
        ImGui::Spacing();

        // ── Description (optional) ────────────────────────────────────────
        ImGui::AlignTextToFramePadding();
        ImGui::Text("Description");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextMultiline("##wf_desc", saveWfState.descBuf, sizeof(saveWfState.descBuf),
                                  ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 3));
        ImGui::Spacing();

        // ── Options ───────────────────────────────────────────────────────
        ImGui::Checkbox("Include output config (folder, format, etc.)",
                        &saveWfState.includeOutputConfig);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Buttons ───────────────────────────────────────────────────────
        ImGui::BeginDisabled(nameEmpty);
        if (ImGui::Button(willOverwrite ? "Overwrite" : "Save", ImVec2(120, 0)))
        {
            const std::string groupStr = saveWfState.groupBuf;  // empty = ungrouped

            const toml::table outputConfig = saveWfState.includeOutputConfig
                ? NodeConfig::buildOutputConfig(app->preferences.outputConfigPref)
                : toml::table();

            const std::string tomlContent = NodeConfig::saveNodeChain(
                app->nodeChain,
                app->nodeChainMutex,
                std::move(outputConfig),
                saveWfState.nameBuf,
                saveWfState.descBuf);

            if (app->WorkflowManager.saveWorkflow(saveWfState.nameBuf,
                                                   saveWfState.descBuf,
                                                   groupStr,
                                                   tomlContent))
            {
                ImGui::InsertNotification({ImGuiToastType::Success, 5000,
                    "Workflow \"%s\" saved.", saveWfState.nameBuf});
            }
            else
            {
                ImGui::InsertNotification({ImGuiToastType::Error, 5000,
                    "Failed to save Workflow \"%s\".", saveWfState.nameBuf});
            }
            closePopup();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::Shortcut(ImGuiKey_Escape))
            closePopup();

        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Delete-confirmation popup
// ─────────────────────────────────────────────────────────────────────────────

void UIComponents_Actions::popup_ConfirmDeleteWorkflow()
{
    const auto closePopup = [this]()
    {
        pendingDialog = PendingDialog::Null;
        ImGui::CloseCurrentPopup();
    };

    if (ImGui::BeginPopupModal("##delete_workflow_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Delete Workflow?");
        ImGui::Spacing();
        ImGui::BulletText("%s", pendingDeleteWorkflow.name.c_str());
        if (!pendingDeleteWorkflow.group.empty())
        {
            ImGui::SameLine(0, 4);
            ImGui::TextDisabled("[%s]", pendingDeleteWorkflow.group.c_str());
        }
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                           "This will permanently delete the workflow file.");
        ImGui::Separator();

        if (ImGui::Button("Delete", ImVec2(120, 0)))
        {
            if (app->WorkflowManager.deleteWorkflow(pendingDeleteWorkflow))
            {
                ImGui::InsertNotification({ImGuiToastType::Success, 4000,
                    "Workflow \"%s\" deleted.", pendingDeleteWorkflow.name.c_str()});
            }
            else
            {
                ImGui::InsertNotification({ImGuiToastType::Error, 5000,
                    "Failed to delete Workflow \"%s\".", pendingDeleteWorkflow.name.c_str()});
            }
            closePopup();
        }

        ImGui::SameLine();

        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::Shortcut(ImGuiKey_Escape))
            closePopup();

        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Reveal in file manager
// ─────────────────────────────────────────────────────────────────────────────

void UIComponents_Actions::subroutine_RevealWorkflow(const WorkflowDescriptor& w)
{
    const std::filesystem::path parent = w.filePath.parent_path();
#ifdef _WIN32
    ShellExecuteW(NULL, L"explore", parent.wstring().c_str(), NULL, NULL, SW_SHOWDEFAULT);
#elif defined(__APPLE__)
    const std::string cmd = "open \"" + parent.u8string() + "\"";
    std::system(cmd.c_str());   // NOLINT(concurrency-mt-unsafe)
#else
    const std::string cmd = "xdg-open \"" + parent.u8string() + "\"";
    std::system(cmd.c_str());   // NOLINT(concurrency-mt-unsafe)
#endif
}

// ─────────────────────────────────────────────────────────────────────────────
// Rename popup
// ─────────────────────────────────────────────────────────────────────────────

void UIComponents_Actions::popup_RenameWorkflow()
{
    const auto closePopup = [this]()
    {
        pendingDialog = PendingDialog::Null;
        ImGui::CloseCurrentPopup();
    };

    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("##rename_workflow", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SeparatorText("Rename Workflow");
        ImGui::Spacing();
        ImGui::TextDisabled("Current name: %s", pendingRenameWorkflow.name.c_str());
        ImGui::Spacing();

        ImGui::Text("New Name *");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputText("##rn_name", renameWfState.nameBuf, sizeof(renameWfState.nameBuf));

        const bool nameEmpty = (renameWfState.nameBuf[0] == '\0');
        if (nameEmpty)
        {
            ImGui::SameLine(0, 6);
            ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f), "(required)");
        }

        // Conflict warning (exclude self — same name is not really a conflict)
        const bool isConflict = !nameEmpty
            && std::string(renameWfState.nameBuf) != pendingRenameWorkflow.name
            && app->WorkflowManager.nameExistsInGroup(renameWfState.nameBuf, pendingRenameWorkflow.group);
        if (isConflict)
        {
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(1.f, 0.45f, 0.45f, 1.f),
                ICON_FA_EXCLAMATION_TRIANGLE " This name is already taken in the same group.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::BeginDisabled(nameEmpty || isConflict);
        if (ImGui::Button("Rename", ImVec2(120, 0)))
        {
            if (app->WorkflowManager.renameWorkflow(pendingRenameWorkflow, renameWfState.nameBuf))
            {
                ImGui::InsertNotification({ImGuiToastType::Success, 4000,
                    "Renamed to \"%s\".", renameWfState.nameBuf});
            }
            else
            {
                ImGui::InsertNotification({ImGuiToastType::Error, 5000,
                    "Failed to rename Workflow \"%s\".", pendingRenameWorkflow.name.c_str()});
            }
            closePopup();
        }
        ImGui::EndDisabled();

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::Shortcut(ImGuiKey_Escape))
            closePopup();

        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Change Group popup
// ─────────────────────────────────────────────────────────────────────────────

void UIComponents_Actions::popup_ChangeGroupWorkflow()
{
    const auto closePopup = [this]()
    {
        pendingDialog = PendingDialog::Null;
        ImGui::CloseCurrentPopup();
    };

    ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always);
    if (ImGui::BeginPopupModal("##change_group_workflow", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::SeparatorText("Change Group");
        ImGui::Spacing();
        ImGui::TextDisabled("Workflow: %s", pendingChangeGrpWorkflow.name.c_str());
        ImGui::TextDisabled("Current group: %s",
            pendingChangeGrpWorkflow.group.empty() ? "(none)" : pendingChangeGrpWorkflow.group.c_str());
        ImGui::Spacing();

        ImGui::Text("New Group");

        constexpr float kPickBtnWidth = 26.0f;
        ImGui::SetNextItemWidth(-kPickBtnWidth - ImGui::GetStyle().ItemSpacing.x);
        ImGui::InputText("##cg_group", changeGrpState.groupBuf, sizeof(changeGrpState.groupBuf));

        ImGui::SameLine(0, ImGui::GetStyle().ItemSpacing.x);
        ImGui::SetNextItemWidth(kPickBtnWidth);
        if (ImGui::BeginCombo("##cg_group_pick", "", ImGuiComboFlags_NoPreview))
        {
            const auto existingGroups = app->WorkflowManager.listGroups();
            if (existingGroups.empty())
                ImGui::TextDisabled("(no groups yet)");
            for (const auto& g : existingGroups)
            {
                if (ImGui::Selectable(g.c_str()))
                {
                    std::strncpy(changeGrpState.groupBuf, g.c_str(), sizeof(changeGrpState.groupBuf) - 1);
                    changeGrpState.groupBuf[sizeof(changeGrpState.groupBuf) - 1] = '\0';
                }
            }
            ImGui::EndCombo();
        }
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
            ImGui::SetTooltip("Pick from existing groups");
        if (changeGrpState.groupBuf[0] == '\0')
            ImGui::TextDisabled("  Leave empty to move to Ungrouped");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Move", ImVec2(120, 0)))
        {
            const std::string newGroup = changeGrpState.groupBuf;
            if (app->WorkflowManager.moveWorkflowToGroup(pendingChangeGrpWorkflow, newGroup))
            {
                ImGui::InsertNotification({ImGuiToastType::Success, 4000,
                    "Moved \"%s\" to group \"%s\".",
                    pendingChangeGrpWorkflow.name.c_str(),
                    newGroup.empty() ? "(Ungrouped)" : newGroup.c_str()});
            }
            else
            {
                ImGui::InsertNotification({ImGuiToastType::Error, 5000,
                    "Failed to move Workflow \"%s\".", pendingChangeGrpWorkflow.name.c_str()});
            }
            closePopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::Shortcut(ImGuiKey_Escape))
            closePopup();

        ImGui::EndPopup();
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// Overwrite-with-current confirmation popup
// ─────────────────────────────────────────────────────────────────────────────

void UIComponents_Actions::popup_ConfirmOverwriteWorkflow()
{
    const auto closePopup = [this]()
    {
        pendingDialog = PendingDialog::Null;
        ImGui::CloseCurrentPopup();
    };

    if (ImGui::BeginPopupModal("##overwrite_workflow_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Overwrite Workflow?");
        ImGui::Spacing();
        ImGui::BulletText("%s", pendingOverwriteWorkflow.name.c_str());
        if (!pendingOverwriteWorkflow.group.empty())
        {
            ImGui::SameLine(0, 4);
            ImGui::TextDisabled("[%s]", pendingOverwriteWorkflow.group.c_str());
        }
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
            ICON_FA_EXCLAMATION_TRIANGLE " The current node chain will overwrite this workflow.");
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f),
            "  The existing content will be lost.");
        ImGui::Separator();

        if (ImGui::Button("Overwrite", ImVec2(120, 0)))
        {
            const toml::table outputConfig = toml::table();  // no output config on quick overwrite
            const std::string tomlContent = NodeConfig::saveNodeChain(
                app->nodeChain,
                app->nodeChainMutex,
                std::move(outputConfig),
                pendingOverwriteWorkflow.name,
                pendingOverwriteWorkflow.description);

            if (app->WorkflowManager.saveWorkflow(pendingOverwriteWorkflow.name,
                                                   pendingOverwriteWorkflow.description,
                                                   pendingOverwriteWorkflow.group,
                                                   tomlContent))
            {
                ImGui::InsertNotification({ImGuiToastType::Success, 4000,
                    "Workflow \"%s\" overwritten.", pendingOverwriteWorkflow.name.c_str()});
            }
            else
            {
                ImGui::InsertNotification({ImGuiToastType::Error, 5000,
                    "Failed to overwrite Workflow \"%s\".", pendingOverwriteWorkflow.name.c_str()});
            }
            closePopup();
        }

        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)) || ImGui::Shortcut(ImGuiKey_Escape))
            closePopup();

        ImGui::EndPopup();
    }
}
