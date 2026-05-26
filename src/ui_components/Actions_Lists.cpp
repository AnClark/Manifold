#include "Actions.hpp"
#include "Main.hpp"
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
