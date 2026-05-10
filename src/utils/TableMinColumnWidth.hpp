#pragma once

#include <imgui_internal.h>

namespace ImGuiHack
{

/**
 * @brief HACK: Set minimum column width for the current ImGui table.
 * ImGui's public API does not provide a way to set minimum column width, but it can be achieved
 * by directly modifying the internal ImGuiTable struct.
 * 
 * @note This function should be called **RIGHT BEFORE** ImGui::EndTable() in your table code block.
 * The width is applied to all columns in the current table.
 */
inline static void SetTableMinColumnWidth(float width)
{
    ImGuiTable* table = ImGui::GetCurrentTable();
    if (table)
        table->MinColumnWidth = width;
}

}
