#include "Main.hpp"
#include "base/ProcessorRegistry.hpp"

#include "imgui.h"

void ManifoldApp::_initProcessorList()
{
    processorList.clear();

    const auto processorIdList = ProcessorRegistry::getInstance().listAll();
    for (auto item : processorIdList)
    {
        processorList.push_back(ProcessorRegistry::getInstance().create(item.c_str()));
    }
}

void ManifoldApp::UI_Actions()
{

    if (ImGui::Begin("Actions"))
    {
        ImGui::Text("Available processors:");
        for (auto item = processorList.begin(); item != processorList.end(); item++)
        {
            ImGui::BulletText("%s", item->get()->getName());
            ImGui::Indent();
            ImGui::BulletText("%s", item->get()->getDescription());
            ImGui::Unindent();
        }
    }
    ImGui::End();
}