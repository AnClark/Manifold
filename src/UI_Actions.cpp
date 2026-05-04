#include "Main.hpp"
#include "pipeline/NodeRegistry.hpp"

#include "imgui.h"
#include "pipeline/base_nodes/DSPNode.hpp"

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
    if (ImGui::BeginChild("Actions"))
    {
        {
            constexpr static ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_Resizable | ImGuiTableFlags_ContextMenuInBody;

            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 2.0f));

            if (ImGui::BeginTable("Actions_Window_Main", 2, flags, ImGui::GetContentRegionAvail()))
            {
                ImGui::TableNextRow();

                // Left Panel: Actions list
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

                // Right panel: Action configuration
                ImGui::TableSetColumnIndex(1);

                if (ImGui::BeginChild("Actions_Editor", ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize))
                {
                    // DEBUG: Just display the current node chain as text for now. Later this will be the actual configuration panel for each node.
                    ImGui::SeparatorText("Current Node Chain");
                    for (size_t i = 0; i < nodeChain.size(); i++)
                    {
                        {
                            const auto& currentNode = nodeChain[i].get();
                            constexpr auto actionEditorFlags = ImGuiWindowFlags_MenuBar;

                            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
                            ImGui::PushID(reinterpret_cast<uintptr_t>(currentNode));

                            float uiWidth, uiHeight;
                            currentNode->getUiSize(uiWidth, uiHeight);

                            if (ImGui::BeginChild("ActionEditor", ImVec2(uiWidth, uiHeight), ImGuiChildFlags_Borders, actionEditorFlags))
                            {
                                if (ImGui::BeginMenuBar())
                                {
                                    ImGui::Text("[%02llu] %s", i, currentNode->name().c_str());
                                    if (ImGui::Button("Remove", ImVec2(80, 0)))
                                    {
                                        nodeChain.erase(nodeChain.begin() + i);
                                        ImGui::EndMenuBar();
                                        ImGui::EndChild();
                                        ImGui::PopID();
                                        ImGui::PopStyleVar();
                                        break;  // Important: break here to avoid accessing invalid memory after erase
                                    }
                                    ImGui::EndMenuBar();
                                }
#if 0
                            // Print parameter definitions if it's a DSP node (for demonstration)
                            if (nodeChain[i]->nodeHint() == "DSP")
                            {
                                const auto& paramDefs = dspNodeParamDefCache[nodeChain[i]->name()];
                                for (const auto& param : paramDefs)
                                {
                                    ImGui::Text("    - %s (%.2f to %.2f, default %.2f)", param.displayName, param.min, param.max, param.def);
                                }
                            }
#else
                                currentNode->drawUI();
#endif
                                ImGui::EndChild();                                
                            }

                            ImGui::PopID();
                            ImGui::PopStyleVar();

                            // Add a neat margin
                            ImGui::Dummy(ImVec2(0, 8));
                        }
                    } 

                    // Footer: Output settings
                    ImGuiExt::MakeFooter(200.0f);
                    ImGui::Separator();
                    
                    {
                        ImGui::BeginGroup();

                        ImGui::Text("Folder");
                        ImGui::SameLine(0, 50);
                        ImGui::Button("Select folder...");   

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