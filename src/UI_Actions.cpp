#include "Main.hpp"
#include "pipeline/NodeRegistry.hpp"

#include "imgui.h"

void ManifoldApp::UI_Actions()
{
    if (ImGui::Begin("Actions"))
    {
        const NodeRegistry& reg = NodeRegistry::getInstance();
        const auto categories   = reg.listCategories();

        for (const auto& cat : categories)
        {
            if (ImGui::CollapsingHeader(cat.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            {
                for (const NodeDescriptor* d : reg.listByCategory(cat))
                {
                    ImGui::BulletText("%s", d->displayName.c_str());
                    ImGui::Indent();
                    ImGui::TextDisabled("%s", d->description.c_str());
                    ImGui::Unindent();
                }
            }
        }
    }
    ImGui::End();
}