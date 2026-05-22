#include "Actions.hpp"

#include "Main.hpp"
#include "config/Config.hpp"

#include <imgui.h>
#include "utils/NFDIncludes.h"  // IWYU pragma: keep
#include "ImGuiNotify_MOD.hpp"

#include "../fonts/IconFontAwesome5_Unique.h"
#include "IconsFontAwesome6.h"  // For icon fonts. Actually suitable for both FA5 and FA6

#include <mutex>

void UIComponents_Actions::subUI_NodeChainView()
{
    // ── Drag-and-drop reorder state (persists across frames) ─────────────
    int&  s_dragSourceIdx = app->dragDropState.dragSourceIdx;   // index of item being dragged (-1 = none)
    int&  s_dropTargetIdx = app->dragDropState.dropTargetIdx;   // insertion point (0..N)
    bool& s_isDragging    = app->dragDropState.isDragging;     // true once mouse moved past threshold

    // Per-frame bounding data for each item (screen Y coords)
    const size_t chainSize = app->nodeChain.size();
    std::vector<float> itemTopY(chainSize, 0.0f);
    std::vector<float> itemBotY(chainSize, 0.0f);

    // FontAwesome 5 font is at Fonts[1] (loaded separately in Main.cpp)
    ImFont* faFont = (ImGui::GetIO().Fonts->Fonts.Size > 1)
                        ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;

    for (size_t i = 0; i < chainSize; i++)
    {
        {
            const auto& currentNode = app->nodeChain[i].get();
            constexpr auto actionEditorFlags = 0;

            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);

            // IMPORTANT: Make the single Node editor unique to avoid conflict  
            ImGui::PushID(reinterpret_cast<uintptr_t>(currentNode));   

            float uiWidth, uiHeight;
            currentNode->getUiSize(uiWidth, uiHeight);

            constexpr float toolbarHeight = 36.0f;
            const bool isUiCollapsed = currentNode->isUiCollapsed();
            if (isUiCollapsed || uiHeight < toolbarHeight)
                uiHeight = toolbarHeight;

            // Record top Y of this item (screen coords) before drawing
            itemTopY[i] = ImGui::GetCursorScreenPos().y;

            // Dim the item currently being dragged so position is visually clear
            const bool isBeingDragged = s_isDragging && (s_dragSourceIdx == (int)i);
            if (isBeingDragged)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);

            if (ImGui::BeginChild("SingleActionEditor", ImVec2(uiWidth, uiHeight), ImGuiChildFlags_Borders, actionEditorFlags))
            {
                // Topbar
                ImGui::BeginGroup();
                {
                    // ── Drag handle ──────────────────────────────────────
                    {
                        if (faFont) ImGui::PushFont(faFont, 14.0f);
                        ImGui::TextUnformatted(ICON_FA_GRIP_VERTICAL);
                        if (faFont) ImGui::PopFont();

                        if (ImGui::IsItemHovered())
                            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip("Drag to reorder node chain");

                        // Initiate drag when grip is pressed
                        if (ImGui::IsItemHovered()
                            && ImGui::IsMouseDown(ImGuiMouseButton_Left)
                            && s_dragSourceIdx == -1)
                            s_dragSourceIdx = (int)i;
                    }
                    ImGui::SameLine(0, 10);
                    // ─────────────────────────────────────────────────────

                    // ── UI expand/collapse toggler ───────────────────────
                    {
                        if (faFont) ImGui::PushFont(faFont, 16.0f);
                        {
                            ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0, 0, 0, 0));
                            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0, 0, 0, 0));
                            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0, 0, 0, 0));
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                            if (ImGui::Button(isUiCollapsed ? ICON_FA_CARET_RIGHT : ICON_FA_CARET_DOWN, ImVec2(16.0f, 0)))
                                currentNode->collapseUI(!isUiCollapsed);
                            ImGui::PopStyleVar();
                            ImGui::PopStyleColor(3);
                        }
                        if (faFont) ImGui::PopFont();

                        if (ImGui::IsItemHovered())
                            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip(isUiCollapsed ? "Expand node UI" : "Collapse node UI");
                    }
                    ImGui::SameLine(0, 8);
                    // ─────────────────────────────────────────────────────

                    ImGui::Text("[%02llu] %s", i, currentNode->name().c_str());
                    ImGui::SameLine();

                    constexpr float toolButtonWidth = 25.0f;
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - toolButtonWidth);
                    if (faFont) ImGui::PushFont(faFont, 14.0f);
                    if (ImGui::Button(ICON_FA_BACKSPACE, ImVec2(toolButtonWidth, 0)))
                    {
                        // Reset drag state on remove
                        app->dragDropState.reset();

                        {
                            std::scoped_lock lock(app->nodeChainMutex);
                            app->nodeChain.erase(app->nodeChain.begin() + i);
                        }

                        // IMPORTANT:
                        // After erasing the node, the current ImGui group for this item becomes invalid (since the underlying node object is destroyed),
                        // so we must give end to the rest of the code in this block with a break statement to avoid messing up the ImGui state.
                        if (faFont) ImGui::PopFont();
                        ImGui::EndGroup();
                        ImGui::EndChild();
                        if (isBeingDragged) ImGui::PopStyleVar(); // pop Alpha
                        ImGui::PopID();
                        ImGui::PopStyleVar();                      // pop ChildRounding
                        break;  // IMPORTANT: break here to avoid accessing invalid memory after erase
                    }
                    if (faFont) ImGui::PopFont();
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                        ImGui::SetTooltip("Remove current node");

                    if (!isUiCollapsed)
                        ImGui::Separator();
                    ImGui::EndGroup();
                }

                // Node-specific UI
                if (!isUiCollapsed)
                {
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10.0f, 2.0f));
                    if (ImGui::BeginChild("NodeSpecificUI", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding))
                    {
                        currentNode->drawUI();
                    }
                    ImGui::EndChild();
                    ImGui::PopStyleVar();
                }
            }
            ImGui::EndChild(); 

            if (isBeingDragged)
                ImGui::PopStyleVar(); // pop Alpha

            // Record bottom Y after child ends
            itemBotY[i] = ImGui::GetCursorScreenPos().y;

            ImGui::PopID();
            ImGui::PopStyleVar();

            // Add a neat margin
            ImGui::Dummy(ImVec2(0, 8));
        }
    }

    // ── Drag-and-drop logic (runs every frame after the list loop) ────────
    app->_dragDropIdle(itemTopY, itemBotY);
}

static constexpr ImVec2 toolButtonSize = { UIComponents_Actions::toolChainBtnWidth, UIComponents_Actions::toolChainBtnHeight };

void UIComponents_Actions::button_ExportNodeChainToFile()
{
    ImGui::PushFont(NULL, 12.0f);
    if (ImGui::Button(ICON_FA_FILE_EXPORT, toolButtonSize))
    {
        toml::table outputConfig = app->exportNodeChainWithOutputConfig
                                    ? NodeConfig::buildOutputConfig(app->preferences.outputConfigPref)
                                    : toml::table();
        std::string outputTOML = NodeConfig::saveNodeChain(app->nodeChain, app->nodeChainMutex, std::move(outputConfig));

        // Open a Save dialog so the user can specify the destination TOML file.
        nfdu8filteritem_t tomlFilter[] = { { "TOML Config", "toml" } };
        nfdwindowhandle_t parentWindow = {};
        NFD_GetNativeWindowFromGLFWWindow(app->getWindow(), &parentWindow);
        NFD::UniquePath savePath;

        nfdresult_t result = NFD::SaveDialog(savePath, tomlFilter, 1, nullptr, u8"node_chain.toml", parentWindow);
        if (result == NFD_OKAY)
        {
            // Write the serialized TOML string to the selected file.
            try {
                // Use u8path() to convert the UTF-8 path from NFD to a filesystem::path,
                // which on Windows internally holds a UTF-16 path — allowing ofstream to
                // correctly open files with non-ASCII (e.g. CJK) characters in the path.
                std::ofstream ofs(std::filesystem::u8path(savePath.get()), std::ios::out | std::ios::trunc);
                if (ofs)
                    ofs << outputTOML;

                LOG_INFOF("Actions", "Exported Node Chain to file: %s", savePath.get());
                ImGui::InsertNotification({ImGuiToastType::Success, 5000, "Successfully exported Node Chain."});
            } catch (std::exception &e) {
                constexpr const char* errMsgTemplate = "Failed to export node chain:\n %s";
                LOG_ERRORF("Actions", errMsgTemplate, e.what());
                ImGui::InsertNotification({ImGuiToastType::Error, 5000, errMsgTemplate, e.what()});
            }
        }
        else if (result == NFD_ERROR)
        {
            auto NFDError = NFD::GetError();
            constexpr const char* errMsgTemplate = "Failed to open file dialog:\n %s";
            LOG_ERRORF("Actions", errMsgTemplate, NFDError);
            ImGui::InsertNotification({ImGuiToastType::Error, 5000, errMsgTemplate, NFDError});
        }
    }
    ImGui::PopFont();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Export current Node Chain");    
}

void UIComponents_Actions::button_ImportNodeChainFromFile()
{
    ImGui::PushFont(NULL, 12.0f);
    if (ImGui::Button(ICON_FA_FILE_IMPORT, toolButtonSize))
    {
        // Open a file dialog to select a TOML node chain config file.
        nfdu8filteritem_t tomlFilter[] = { { "TOML Config", "toml" } };
        nfdwindowhandle_t parentWindow = {};
        NFD_GetNativeWindowFromGLFWWindow(app->getWindow(), &parentWindow);
        NFD::UniquePath openPath;

        nfdresult_t result = NFD::OpenDialog(openPath, tomlFilter, 1, nullptr, parentWindow);
        if (result == NFD_OKAY)
        {
            try {
                // Pass &preferences.outputConfigPref directly: if the file contains an [output]
                // section, it is applied in-place to the app's output settings (folder, format,
                // subtype, null-output). If absent, the existing settings are left unchanged.
                NodeConfig::loadNodeChain(openPath.get(), app->nodeChain, app->nodeChainMutex,
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

                LOG_INFOF("Actions", "Imported Node Chain from file: %s", openPath.get());
                ImGui::InsertNotification({ImGuiToastType::Success, 5000, "Successfully imported Node Chain."});
            }
            catch (const char* err) {
                constexpr const char* errMsgTemplate = "Failed to import node chain:\n %s";
                LOG_ERRORF("Actions", errMsgTemplate, err);
                ImGui::InsertNotification({ImGuiToastType::Error, 5000, errMsgTemplate, err});
            }
            catch (const std::exception& e) {
                constexpr const char* errMsgTemplate = "Failed to import node chain:\n %s";
                LOG_ERRORF("Actions", errMsgTemplate, e.what());
                ImGui::InsertNotification({ImGuiToastType::Error, 5000, errMsgTemplate, e.what()});
            }
        }
        else if (result == NFD_ERROR)
        {
            auto NFDError = NFD::GetError();
            constexpr const char* errMsgTemplate = "Failed to open file dialog:\n %s";
            LOG_ERRORF("Actions", errMsgTemplate, NFDError);
            ImGui::InsertNotification({ImGuiToastType::Error, 5000, errMsgTemplate, NFDError});
        }
    }
    ImGui::PopFont();
}

void UIComponents_Actions::button_OpenNodeChainMenu()
{
    ImGui::PushFont(NULL, 12.0f);
    if (ImGui::Button(ICON_FA_BARS, toolButtonSize))
    {
        ImGui::OpenPopup("##Options_of_Node_Chain_View");
    }
    ImGui::PopFont();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Options");
}

void UIComponents_Actions::popup_NodeChainMenu()
{
    if (ImGui::BeginPopup("##Options_of_Node_Chain_View"))
    {
        ImGui::SeparatorText("Options");

        ImGui::Checkbox("Save Output Config with Node Chain", &app->exportNodeChainWithOutputConfig);
        ImGui::SameLine();
        ImGui::TextDisabled("(output folder, format, etc.)");

        ImGui::Checkbox("Import Output Config when importing Node Chain", &app->importNodeChainWithOutputConfig);
        ImGui::SameLine();
        ImGui::TextDisabled("(will overwrite current config)");

        ImGui::Checkbox("Remember recent Output Config", &app->rememberRecentOutputConfigPref);

        ImGui::Spacing();
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 80.0f);
        if (ImGui::Button("Close", ImVec2(80, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }                            
}
