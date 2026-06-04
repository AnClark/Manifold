#include "Actions.hpp"
#include "Main.hpp"
#include "config/Config.hpp"
#include "pipeline/NodeRegistry.hpp"

#include <imgui.h>
#include "ImGuiNotify_MOD.hpp"

#include <mutex>

void UIComponents_Actions::subUI_ActionList()
{
    if (ImGui::BeginChild("Actions_List", ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize))
    {
        const NodeRegistry& reg = NodeRegistry::getInstance();
        const auto categories   = reg.listCategories();

        for (const auto& cat : categories)
        {
            if (ImGui::CollapsingHeader(cat.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (const NodeDescriptor* d : reg.listByCategory(cat))
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.1f, 0.5f)); // Button text align at left, with slight indent
                    const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
                    const ImVec4 btnOrigColor = ImGui::GetStyle().Colors[ImGuiCol_Button];    // Use original default color for list's hover color
                    ImGui::PushStyleColor(ImGuiCol_Button, bg);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnOrigColor);

                    if (ImGui::Button(d->displayName.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                    {
                        try 
                        {
                            auto node = NodeRegistry::getInstance().create(d->id);
                            {
                                std::scoped_lock lock(app->nodeChainMutex);
                                app->nodeChain.emplace_back(std::move(node));
                            }
                        }
                        catch (const std::exception& e)
                        {
                            LOG_ERRORF("Actions", "%s", e.what());
                            ImGui::InsertNotification({ImGuiToastType::Error, 5000, "Failed to add Action Node:\n%s", e.what()});
                        }
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)
                        && d->description.size() > 0)
                        ImGui::SetTooltip("%s", d->description.c_str());

                    ImGui::PopStyleColor(2);
                    ImGui::PopStyleVar();
                }
            }
        }
    }
    ImGui::EndChild();
}

void UIComponents_Actions::subUI_WorkflowList()
{
    if (ImGui::BeginChild("Workflow_List", ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize))
    {
        const auto groups = app->WorkflowManager.listGroups();

        if (!app->WorkflowManager.workflowsCount())
        {
            const ImVec2 availSpace = ImGui::GetContentRegionAvail();
            const ImVec2 refTextSize = ImGui::CalcTextSize("A Workflow is a Node Chain configuration stored in Manifold");

            const ImVec2 cursorPos = ImGui::GetCursorPos();
            ImGui::SetCursorPosX(cursorPos.x + availSpace.x * 0.5 - refTextSize.x * 0.5);
            ImGui::SetCursorPosY(cursorPos.y + availSpace.y * 0.5 - refTextSize.y * 5.2);

            ImGui::BeginGroup();
            ImGui::TextDisabled("No Workflows in Manifold.");
            ImGui::Spacing();
            ImGui::TextDisabled("A Workflow is a Node Chain configuration stored in Manifold\n"
                                     "for future use, just like \"Presets\" of audio plugins.");
            ImGui::Spacing();
            ImGui::TextDisabled("You can add current Node Chain to Workflow list.");
            ImGui::EndGroup();
        }
        else
        for (const auto& group : groups)
        {
            if (ImGui::CollapsingHeader(group.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (const WorkflowDescriptor* w : app->WorkflowManager.listByGroup(group))
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.1f, 0.5f)); // Button text align at left, with slight indent
                    const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
                    const ImVec4 btnOrigColor = ImGui::GetStyle().Colors[ImGuiCol_Button];    // Use original default color for list's hover color
                    ImGui::PushStyleColor(ImGuiCol_Button, bg);
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnOrigColor);

                    if (ImGui::Button(w->name.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                    {
                        subroutine_LoadWorkflow(*w);
                    }
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal) && !w->description.empty())
                        ImGui::SetTooltip("%s", w->description.c_str());

                    ImGui::PopStyleColor(2);
                    ImGui::PopStyleVar();
                }
            }
        }
    }
    ImGui::EndChild();
}

void UIComponents_Actions::subroutine_LoadWorkflow(const WorkflowDescriptor& w)
{
    if (!std::filesystem::exists(w.filePath))
    {
        constexpr const char* errMsgTemplate = "Workflow file does not exist, or path is empty";
        LOG_ERRORF("Actions", errMsgTemplate);
        ImGui::InsertNotification({ImGuiToastType::Error, 5000, errMsgTemplate});
        return;
    }

    try {
        // Pass &preferences.outputConfigPref directly: if the file contains an [output]
        // section, it is applied in-place to the app's output settings (folder, format,
        // subtype, null-output). If absent, the existing settings are left unchanged.
        NodeConfig::loadNodeChain(w.filePath.u8string(), app->nodeChain, app->nodeChainMutex,
                                    app->importNodeChainWithOutputConfig ? &app->preferences.outputConfigPref : nullptr);

        // Reset drag-and-drop state since the node chain structure has changed.
        app->dragDropState.reset();

        // Validate the imported filename template if output config was applied.
        if (app->importNodeChainWithOutputConfig)
        {
            if (auto err = app->preferences.outputConfigPref.filenameTemplate.validate())
            {
                const std::string errMsg = *err;
                LOG_WARNF("Actions",
                            "Imported filename template is invalid (%s) — reset to default",
                            errMsg.c_str());
                app->preferences.outputConfigPref.filenameTemplate = FilenameTemplate::makeDefault();
                ImGui::InsertNotification({ImGuiToastType::Warning, 8000,
                    "Imported filename template is invalid:\n%s\nReset to default.",
                    errMsg.c_str()});
            }
        }

        LOG_INFOF("Actions", "Loaded workflow: %s", w.name.c_str());
        ImGui::InsertNotification({ImGuiToastType::Success, 5000, "Successfully loaded Workflow \"%s\".", w.name.c_str()});
    }
    catch (const char* err) {
        constexpr const char* errMsgTemplate = "Failed to load workflow \"%s\":\n %s";
        LOG_ERRORF("Actions", errMsgTemplate, w.name.c_str(), err);
        ImGui::InsertNotification({ImGuiToastType::Error, 5000, errMsgTemplate, w.name.c_str(), err});
    }
    catch (const std::exception& e) {
        constexpr const char* errMsgTemplate = "Failed to load workflow \"%s\":\n %s";
        LOG_ERRORF("Actions", errMsgTemplate, w.name.c_str(), e.what());
        ImGui::InsertNotification({ImGuiToastType::Error, 5000, errMsgTemplate, w.name.c_str(), e.what()});
    }
}
