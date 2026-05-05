#include "Main.hpp"
#include "pipeline/NodeRegistry.hpp"

#include "imgui.h"
#include "nfd.hpp"
#include "pipeline/base_nodes/DSPNode.hpp"

#include "../fonts/IconFontAwesome5.h"

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
                                        nodeChain.emplace_back(std::move(node));

                                        if (nodeChain.back()->nodeHint() == "DSP")
                                        {
                                            // If it's a DSP node, fetch its parameter definitions and cache them for later use in UI
                                            // (e.g. To load default values)
                                            std::vector<AudioProcessorParam> paramDefs;
                                            dynamic_cast<DSPNode*>(nodeChain.back().get())->fetchProcessorParamList(paramDefs);
                                            const auto name = nodeChain.back()->name();
                                            dspNodeParamDefCache[name] = std::move(paramDefs);
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

                if (ImGui::BeginChild("Actions_Editor", ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize))
                {
                    // DEBUG: Just display the current node chain as text for now. Later this will be the actual configuration panel for each node.
                    ImGui::SeparatorText("Current Node Chain");

                    // ── Drag-and-drop reorder state (persists across frames) ─────────────
                    int&  s_dragSourceIdx = this->dragDropState.dragSourceIdx;   // index of item being dragged (-1 = none)
                    int&  s_dropTargetIdx = this->dragDropState.dropTargetIdx;   // insertion point (0..N)
                    bool& s_isDragging    = this->dragDropState.isDragging;     // true once mouse moved past threshold

                    // Per-frame bounding data for each item (screen Y coords)
                    const size_t chainSize = nodeChain.size();
                    std::vector<float> itemTopY(chainSize, 0.0f);
                    std::vector<float> itemBotY(chainSize, 0.0f);

                    // FontAwesome font is at Fonts[1] (loaded separately in Main.cpp)
                    ImFont* faFont = (ImGui::GetIO().Fonts->Fonts.Size > 1)
                                     ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;

                    for (size_t i = 0; i < chainSize; i++)
                    {
                        {
                            const auto& currentNode = nodeChain[i].get();
                            constexpr auto actionEditorFlags = 0;//ImGuiWindowFlags_MenuBar;

                            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
                            ImGui::PushID(reinterpret_cast<uintptr_t>(currentNode));

                            float uiWidth, uiHeight;
                            currentNode->getUiSize(uiWidth, uiHeight);

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

                                    ImGui::Text("[%02llu] %s", i, currentNode->name().c_str());
                                    ImGui::SameLine();

                                    constexpr float toolButtonWidth = 25.0f;
                                    ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - toolButtonWidth);
                                    if (faFont) ImGui::PushFont(faFont, 14.0f);
                                    if (ImGui::Button(ICON_FA_BACKSPACE, ImVec2(20, 0)))
                                    {
                                        // Reset drag state on remove
                                        dragDropState.reset();

                                        nodeChain.erase(nodeChain.begin() + i);

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

                                    ImGui::Separator();
                                    ImGui::EndGroup();
                                }

                                // Node-specific UI
                                currentNode->drawUI();

                                ImGui::EndChild();                                
                            }

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
                    _dragDropIdle(itemTopY, itemBotY);

                    //
                    // FOOTER: Output settings
                    //
                    ImGuiExt::MakeFooter(200.0f);
                    ImGui::Separator();
                    
                    {
                        ImGui::BeginGroup();

                        ImGui::Text("Folder");
                        ImGui::SameLine(0, 30);
                        
                        {
                            // Calculate background color: 30% lighter than window background
                            const ImVec4 bgColor = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
                            const ImVec4 lighterBg = ImVec4(
                                bgColor.x + (1.0f - bgColor.x) * 0.125f,
                                bgColor.y + (1.0f - bgColor.y) * 0.125f,
                                bgColor.z + (1.0f - bgColor.z) * 0.125f,
                                bgColor.w
                            );
                            
                            // Draw background rectangle for the text area
                            ImDrawList* drawList = ImGui::GetWindowDrawList();
                            ImVec2 textPos = ImGui::GetCursorScreenPos();
                            float textWidth = ImGui::GetContentRegionAvail().x;
                            float textHeight = ImGui::GetTextLineHeight();
                            drawList->AddRectFilled(textPos, ImVec2(textPos.x + textWidth, textPos.y + textHeight), 
                                                    ImGui::ColorConvertFloat4ToU32(lighterBg));
                            
                            if (outputPath.empty())
                                ImGui::TextDisabled("(empty)");
                            else
                                ImGui::Text("%s", outputPath.c_str());
                        }

                        constexpr float selectButtonWidth = 160.0f;
                        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - selectButtonWidth - 5.0f);
                        if (ImGui::Button("Select Output Folder...", ImVec2(selectButtonWidth, 0)))
                        {
                            nfdu8char_t* pickedPath = nullptr;
                            if (NFD::PickFolder(pickedPath) == NFD_OKAY)
                            {
                                outputPath = pickedPath;
                                NFD::FreePath(pickedPath);
                            }
                        }

                        ImGui::EndGroup();    
                    }

                    {
                        ImGui::BeginGroup();

                        ImGui::Text("File Name");

                        ImGui::EndGroup();
                    }
                }
                ImGui::EndChild();

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
    const ImU32 lineColor = IM_COL32(30, 144, 255, 230);
    ImDrawList* drawList  = ImGui::GetWindowDrawList();
    const float x0        = ImGui::GetWindowPos().x + 8.0f;
    const float x1        = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 8.0f;

    drawList->AddLine(
        ImVec2(x0 + 8.0f, indicatorY),
        ImVec2(x1,         indicatorY),
        lineColor, 2.0f);
    drawList->AddCircleFilled(ImVec2(x0 + 4.0f, indicatorY), 4.0f, lineColor);
    drawList->AddCircleFilled(ImVec2(x1,         indicatorY), 4.0f, lineColor);
}
