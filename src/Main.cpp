#include "Main.hpp"
#include "utils/LogManager.hpp"

#include "imgui.h"
#include "nfd.h"

#include "../fonts/DroidSans.hpp"
#include "../fonts/DroidSansFallback.hpp"
#include "../fonts/FontAwesome5.hpp"

void ManifoldApp::onInit()
{
    // Initialize and configure Log Manager
    // - Set log buffer size to 4096 entries
    // - Set log min level to Debug
    LogManager::instance(4096).setMinLevel(LogLevel::Debug);

    // Wire up the node chain pointer so the worker can access it
    singleFileProcessorWorker.setNodeChain(this->nodeChain, this->nodeChainMutex);

    // Initialize NFD
    NFD_Init();

    // Load font with Chinese support
    // 1. Add DroidSans first
    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(DroidSansFont_compressed_data, DroidSansFont_compressed_size, 16.0f, nullptr);
    // 2. Merge DroidSansFallback into DroidSans.
    ImFontConfig config;
    config.MergeMode = true;
    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(DroidSansFallbackFont_compressed_data, DroidSansFallbackFont_compressed_size, 16.0f, &config);

    // Load FontAwesome 5
    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(FontAwesomeTTF_compressed_data, FontAwesomeTTF_compressed_size, 20.0f, nullptr);

    LOG_INFO("Main", "Manifold initialized");
}

void ManifoldApp::onTerminate()
{
    // Stop all pending/on-progress EBU R128 measuring
    //
    // NOTICE: onTerminate() stage won't close workers, so we need to signal them to stop processing and exit their processing loop,
    //         for example, EBUR128Worker::processFile().
    //         When everything is done, workers will stop during deconstruction, and we can be sure that no more processing is running
    //         after onTerminate() returns.
    ebur128Worker.requestCancelProcessing();
    dcOffsetWorker.requestCancelProcessing();

    // Stop all pending/on-progress audio rendering
    singleFileProcessorWorker.requestCancelProcessing();

    // Cleanup NFD
    NFD_Quit();
}

void ManifoldApp::onImGuiDisplay()
{
    // Make window fullscreen
    static constexpr int filesWindowFlag =
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoMove       |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize;
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);

    // FontAwesome font is at Fonts[1] (loaded separately in Main.cpp)
    ImFont* faFont = (ImGui::GetIO().Fonts->Fonts.Size > 1)
                        ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;

    if (ImGui::Begin("Main Window", nullptr, filesWindowFlag))
    {
        // Action bar
        {
            ImGui::BeginGroup();
            auto uiSwitchButton = [this, faFont](const char* label, UIState newState, const char* tooltip)
            {
                if (faFont) ImGui::PushFont(faFont);
                if (ImGui::Button(label, ImVec2(40, 40)))
                {
                    this->uiState = newState;
                }
                if (faFont) ImGui::PopFont();

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                    ImGui::SetTooltip("%s", tooltip);

                ImGui::Dummy(ImVec2(0, 4));
            };

            const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
            const ImVec4 btnOrigColor = ImGui::GetStyle().Colors[ImGuiCol_Button];    // Use original default color for list's hover color
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnOrigColor);

            uiSwitchButton("\uf1c7", pUIFiles, "Files");
            uiSwitchButton("\uf0ae", pUIActions, "Actions");
            uiSwitchButton("\uf1ea", pUILog, "Log");

            ImGui::PopStyleColor(2);

            ImGui::EndGroup();            
        }


        ImGui::SameLine(0, 16);

        ImGui::BeginGroup();
        switch (uiState)
        {
            case pUIFiles:
                UI_Files();
                break;
            case pUIActions:
                UI_Actions();
                break;
            case pUILog:
                UI_Log();
                break;
            default:
                UI_Files();
        }
        ImGui::EndGroup();
    }
    ImGui::End();


}

int main()
{
    ManifoldApp app;
    app.mainLoop();
}
