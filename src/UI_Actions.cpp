#include "Main.hpp"
#include "pipeline/NodeRegistry.hpp"

#include "imgui.h"
#include "utils/NFDIncludes.h"  // IWYU pragma: keep
#include "ImGuiNotify_MOD.hpp"

#include "utils/TableMinColumnWidth.hpp"

#include "../fonts/IconFontAwesome5_Unique.h"

#include <algorithm>

namespace ImGuiExt
{
    /**
     * @brief ImGui extention: Create footer layout.
     * Invoke this method, then any elements created later will be displayed as "footer" in your window.
     */
    void MakeFooter(float footer_height = -1.0f)
    {
        // ========== Footer: Auto stick-to-bottom logic ==========
        // If specify negative value, estimate the total height needed for the Footer area
        // (separator + text + spacing)
        if (footer_height < 0)
            footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

        // Calculate the remaining space from the current cursor to the bottom of the window
        float spacing = ImGui::GetContentRegionAvail().y - footer_height;

        // If there is still extra space, insert a Dummy to push the Footer to the bottom
        if (spacing > 0.0f)
            ImGui::Dummy(ImVec2(0.0f, spacing));

        // Add a dummy space to make the footer has enough height
        // To let the widgets draw in the right place, current cursor position should be stored,
        // then restore after the dummy space.
        const ImVec2 cursor_pos_backup = ImGui::GetCursorPos();
        ImGui::Dummy(ImVec2(0, footer_height - (footer_height * 0.05f)));   // reduce height a bit to avoid showing scrollbar too early
        ImGui::SetCursorPos(cursor_pos_backup);
    }
}

void ManifoldApp::UI_Actions()
{
    //
    // LAMBDAS: Sub widgets / sub procedures
    //
    auto _drawActionBlock = [this]() {

    };

    //
    // Main Window
    //
    if (ImGui::BeginChild("Actions"))
    {
        {
            constexpr static ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_Resizable | ImGuiTableFlags_ContextMenuInBody;

            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 2.0f));

            if (ImGui::BeginTable("Actions_Window_Main", 2, flags, ImGui::GetContentRegionAvail()))
            {
                ImGui::TableSetupColumn("##Action_List", ImGuiTableColumnFlags_WidthStretch, preferences.uiPref.actionLeftPanelWeight);
                ImGui::TableSetupColumn("##Node_Chain", ImGuiTableColumnFlags_WidthStretch, preferences.uiPref.actionRightPanelWeight);
                
                // Save current column width weight to preferences storage
                const float tableWidth = ImGui::GetCurrentTable()->ColumnsAutoFitWidth;
                preferences.uiPref.actionLeftPanelWeight = ImGui::GetCurrentTable()->Columns[0].WidthGiven / tableWidth;
                preferences.uiPref.actionRightPanelWeight = ImGui::GetCurrentTable()->Columns[1].WidthGiven / tableWidth;

                ImGui::TableNextRow();

                //
                // LEFT PANEL: Actions list
                //
                ImGui::TableSetColumnIndex(0);

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
                                            std::scoped_lock lock(nodeChainMutex);
                                            nodeChain.emplace_back(std::move(node));
                                        }
                                    }
                                    catch (const std::exception& e)
                                    {
                                        // TODO: Handle error by showing a message box or notification on UI. To be implemented.
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

                //
                // RIGHT PANEL: Action configuration
                //
                ImGui::TableSetColumnIndex(1);

                if (ImGui::BeginChild("Actions_Editor", ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
                {
                    {
                        ImGui::BeginGroup();

                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Current Node Chain");
                        ImGui::SameLine();

                        ImGui::SetCursorPosX(ImGui::GetCursorPosX() +  ImGui::GetContentRegionAvail().x - uiActions.toolChainBtnWidth * 4);

                        uiActions.button_ExportNodeChainToFile();

                        ImGui::SameLine();

                        uiActions.button_ImportNodeChainFromFile();

                        ImGui::SameLine(0, 12.0f);

                        // [Button] Menu
                        {
                            uiActions.button_OpenNodeChainMenu();

                            uiActions.popup_NodeChainMenu();
                        }

                        ImGui::EndGroup();
                    }
                    ImGui::Separator();

                    // ── Footer pane state (static: persists across frames) ────────────────
                    float& footerPaneHeight    = preferences.uiPref.actionFooterPaneHeight;
                    bool&  footerPaneCollapsed = preferences.uiPref.actionFooterPaneCollapsed;
                    constexpr float kFooterMinH = 80.0f;
                    constexpr float kFooterMaxH = 600.0f;

                    // [Top node-list child] occupies all space above the footer pane
                    //
                    // Height accounting: each child/widget followed by a cursor advance of (height + ItemSpacing.y).
                    //   Total = (topChildH + IS) + (splitterH + IS) + (footerH + IS)  [when footer visible]
                    //         = (topChildH + IS) + (splitterH + IS)                   [when footer collapsed]
                    // To keep total == availH we absorb the extra trailing IS into splitterRowH.
                    {
                        constexpr float kTopMinH = 40.0f;
                        const float availH       = ImGui::GetContentRegionAvail().y;
                        const float IS           = ImGui::GetStyle().ItemSpacing.y;
                        // When footer is visible there is one extra trailing IS (after Actions_Footer) to absorb.
                        const float splitterRowH = ImGui::GetFrameHeight() + IS * (footerPaneCollapsed ? 2.0f : 3.0f);
                        // Clamp footer height so the node list always has at least kTopMinH.
                        if (!footerPaneCollapsed)
                            footerPaneHeight = std::clamp(footerPaneHeight, kFooterMinH,
                                                          std::max(kFooterMinH, availH - splitterRowH - kTopMinH));
                        const float footerH   = footerPaneCollapsed ? 0.0f : footerPaneHeight;
                        const float topChildH = availH - splitterRowH - footerH;
                        ImGui::BeginChild("Actions_NodeList", ImVec2(0, std::max(topChildH, kTopMinH)));
                    }

                    uiActions.subUI_NodeChainView();

                    ImGui::EndChild(); // [Top node-list child] end

                    // ── Footer pane splitter bar ──────────────────────────────────────────
                    {
                        const float frameH = ImGui::GetFrameHeight();

                        // Collapse / expand toggle button
                        if (ImGui::ArrowButton("##footer_toggle",
                                               footerPaneCollapsed ? ImGuiDir_Up : ImGuiDir_Down))
                            footerPaneCollapsed = !footerPaneCollapsed;
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                            ImGui::SetTooltip(footerPaneCollapsed
                                              ? "Expand output settings"
                                              : "Collapse output settings");

                        ImGui::SameLine(0, 4);
                        ImGui::AlignTextToFramePadding();
                        ImGui::TextDisabled("Output Settings");
                        ImGui::SameLine(0, 8);

                        // Drag handle: fills the remaining row; dragging adjusts footer height
                        const float  dragW   = std::max(ImGui::GetContentRegionAvail().x, 4.0f);
                        const ImVec2 dragMin = ImGui::GetCursorScreenPos();
                        ImGui::InvisibleButton("##footer_splitter", ImVec2(dragW, frameH));
                        if (ImGui::IsItemActive() && !footerPaneCollapsed)
                        {
                            footerPaneHeight -= ImGui::GetIO().MouseDelta.y;
                            footerPaneHeight  = std::clamp(footerPaneHeight, kFooterMinH, kFooterMaxH);
                        }
                        if ((ImGui::IsItemHovered() || ImGui::IsItemActive()) && !footerPaneCollapsed)
                            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);

                        // Draw a subtle separator line through the drag area
                        const float lineY = dragMin.y + frameH * 0.5f;
                        ImGui::GetWindowDrawList()->AddLine(
                            ImVec2(dragMin.x,          lineY),
                            ImVec2(dragMin.x + dragW,  lineY),
                            ImGui::GetColorU32(ImGuiCol_Separator), 1.0f);
                    }

                    // ── Footer pane: Output settings ──────────────────────────────────────
                    if (!footerPaneCollapsed)
                    {
                    ImGui::BeginChild("Actions_Footer", ImVec2(0, footerPaneHeight), ImGuiChildFlags_Borders);

                    constexpr float kLabelW = 120.0f;   // Label width

                    {
                        ImGui::BeginGroup();
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("Folder");
                        ImGui::SameLine(kLabelW, 0);

                        uiActions.button_SelectOutputFolder();

                        ImGui::EndGroup();    
                    }

                    ImGui::Spacing();

                    {
                        // ── File Name ─────────────────────────────────────────

                        ImGui::BeginGroup();
                        ImGui::AlignTextToFramePadding();
                        ImGui::Text("File Name");
                        ImGui::SameLine(kLabelW, 0);

                        uiActions.button_FileName();

                        ImGui::EndGroup();
                    }

                    ImGui::Spacing();

                    // Output format selector
                    {
                        ImGui::BeginGroup();

                        // ── Container format ──────────────────────────────
                        {
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Format");
                            ImGui::SameLine(kLabelW, 0);
                            
                            uiActions.combo_SelectContainerFormat();
                        }

                        ImGui::Spacing();

                        // ── Sample subtype ────────────────────────────────
                        {
                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Subtype");
                            ImGui::SameLine(kLabelW);
                            
                            uiActions.combo_SelectSampleSubtype();
                        }

                        ImGui::EndGroup();
                    }

                    ImGui::Spacing();

                    // ── Null output toggle ───────────────────────────────
                    ImGui::Checkbox("Null Output", &nullOutput);
                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                    {
                        if (ImGui::BeginTooltip())
                        {
                            ImGui::Text("Process audio without writing output files.");
                            ImGui::Separator();
                            ImGui::Text("If you only analyze audio files (e.g. via Loudness Compliance), but not need to produce any artifacts,\n"
                            "this mode would be useful.");
                            ImGui::EndTooltip();
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("(Useful for analysis-only pipelines)");

                    ImGui::Dummy(ImVec2(0, 4));

                    {
                        ImGui::Spacing();

                        // ── Process button ───────────────────────────────────
                        const bool canProcess = uiActions.query_CanProcess();
                        ImGui::BeginDisabled(!canProcess);
                        if (ImGui::Button("Process All Files", ImVec2(-1, 28.0f)))
                        {
                            uiActions.command_StartProcessingAllFiles();
                        }
                        ImGui::EndDisabled();

                        uiActions.info_ShowProcessingHints();

                        // ── Latest run status ────────────────────────────────
                        uiActions.subUI_ShowLatestRunStatus();
                    }
                    ImGui::EndChild(); // end Actions_Footer
                    } // end if (!footerPaneCollapsed)
                }
                ImGui::EndChild();

                // HACK: Set minimum column width
                // Make sure reference text (for example, the warning/tip text for "Process All Files" button) can be fully shown.
                constexpr const char* referenceText = ICON_FA_EXCLAMATION_TRIANGLE " Go to Files, add files to the file list to enable processing.";
                const float referenceTextWidth = ImGui::CalcTextSize(referenceText).x;
                ImGuiHack::SetTableMinColumnWidth(referenceTextWidth + ImGui::GetStyle().CellPadding.x * 2.0f);

                // Apply minimum column width to avoid the panel getting too narrow when resizing window
                // (To test, maximize the window, dragging it to the minimum width, then restore the window)
                if (ImGui::GetCurrentTable()->Columns[0].WidthGiven < referenceTextWidth)
                    ImGui::TableSetColumnWidth(0, referenceTextWidth);

                ImGui::EndTable();
            }

            ImGui::PopStyleVar();
        }
    }
    ImGui::EndChild();
}

void ManifoldApp::_dragDropIdle(const std::vector<float>& itemTopY, const std::vector<float>& itemBotY)
{
    int&  s_dragSourceIdx = dragDropState.dragSourceIdx;
    int&  s_dropTargetIdx = dragDropState.dropTargetIdx;
    bool& s_isDragging    = dragDropState.isDragging;

    if (s_dragSourceIdx < 0 || s_dragSourceIdx >= (int)nodeChain.size())
        return;

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        // Mouse released: perform reorder if drop position is meaningful
        if (s_isDragging
            && s_dropTargetIdx >= 0
            && s_dropTargetIdx != s_dragSourceIdx
            && s_dropTargetIdx != s_dragSourceIdx + 1)
        {
            std::scoped_lock lock(nodeChainMutex);
            auto node = std::move(nodeChain[s_dragSourceIdx]);
            nodeChain.erase(nodeChain.begin() + s_dragSourceIdx);
            int insertAt = (s_dropTargetIdx > s_dragSourceIdx)
                           ? s_dropTargetIdx - 1
                           : s_dropTargetIdx;
            nodeChain.insert(nodeChain.begin() + insertAt, std::move(node));
        }
        
        dragDropState.reset();
        return;
    }

    // Activate drag once mouse moves beyond threshold
    if (!s_isDragging)
    {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 3.0f);
        if (delta.x != 0.0f || delta.y != 0.0f)
            s_isDragging = true;
    }

    if (!s_isDragging || (int)itemTopY.size() != (int)nodeChain.size())
        return;

    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // Compute drop target: compare mouse Y against each item's midpoint
    const float mouseY = ImGui::GetMousePos().y;
    s_dropTargetIdx = (int)nodeChain.size(); // default: after last
    for (int j = 0; j < (int)nodeChain.size(); j++)
    {
        const float midY = (itemTopY[j] + itemBotY[j]) * 0.5f;
        if (mouseY < midY)
        {
            s_dropTargetIdx = j;
            break;
        }
    }

    // Determine indicator line Y (between items / at edges)
    float indicatorY = 0.0f;
    if (!itemTopY.empty())
    {
        if (s_dropTargetIdx <= 0)
            indicatorY = itemTopY[0];
        else if (s_dropTargetIdx >= (int)nodeChain.size())
            indicatorY = itemBotY.back();
        else
            indicatorY = itemTopY[s_dropTargetIdx];
    }

    // Draw Windows-Explorer-style drop indicator:
    // a horizontal line with filled circle caps
    // NOTE: I've done a hack to avoid the indicator being overlapped by scrollbar.
    const ImU32 lineColor = IM_COL32(30, 144, 255, 230);
    ImDrawList* drawList  = ImGui::GetWindowDrawList();
    const float x0        = ImGui::GetWindowPos().x + 8.0f;
    const float x1        = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() 
                            - (ImGui::GetWindowScrollbarID(ImGui::GetCurrentWindow(), ImGuiAxis_Y) ? ImGui::GetStyle().ScrollbarSize + 12.0f : 8.0f);

    drawList->AddLine(
        ImVec2(x0 + 8.0f, indicatorY),
        ImVec2(x1,         indicatorY),
        lineColor, 2.0f);
    drawList->AddCircleFilled(ImVec2(x0 + 4.0f, indicatorY), 4.0f, lineColor);
    drawList->AddCircleFilled(ImVec2(x1,         indicatorY), 4.0f, lineColor);
}
