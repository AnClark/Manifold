#include "Actions.hpp"

#include "Main.hpp"

#include <imgui.h>

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

                    ImGui::Separator();
                    ImGui::EndGroup();
                }

                // Node-specific UI
                currentNode->drawUI();       
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
