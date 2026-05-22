#include "Main.hpp"
#include "utils/LogManager.hpp"

#include "imgui.h"
#include "nfd.h"
#include "ImGuiNotify_MOD.hpp"

#include "../fonts/DroidSans.hpp"
#include "../fonts/DroidSansFallback.hpp"
#include "../fonts/FontAwesome5.hpp"

// Font Awesome 6 bundled with ImGuiNotify
#include "fonts/fa-solid-900.h"

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
    // 3. Merge FontAwesome 6 icons into the same font (for ImGuiNotify's icons)
    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(fa_solid_900_compressed_data, fa_solid_900_compressed_size, 20.0f, &config);

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
#ifdef ENABLE_PARALLEL_ANALYZING
    analyzerDispatcher.requestCancelProcessing();
#else
    ebur128Worker.requestCancelProcessing();
    dcOffsetWorker.requestCancelProcessing();
#endif

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
                const ImVec2 buttonPos = ImGui::GetCursorPos();  // Store the current position for drawing indicator later 
                constexpr ImVec2 buttonSize = ImVec2(40, 40);
                const bool isHovered = ImGui::IsMouseHoveringRect(
                    buttonPos, ImVec2(buttonPos.x + buttonSize.x, buttonPos.y + buttonSize.y)
                ) && ImGui::IsWindowHovered();  // Dear ImGui does not support changing text color on item hovering, so we need to implement ourselves.

                if (faFont) ImGui::PushFont(faFont);
                const bool useInactiveColor = (this->uiState != newState) && !isHovered;  // NOTE: Store the condition flag in a variable to avoid Push/Pop mismatch
                if (useInactiveColor) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 127));
                if (ImGui::Button(label, buttonSize))
                {
                    this->uiState = newState;
                }
                if (useInactiveColor) ImGui::PopStyleColor();
                if (faFont) ImGui::PopFont();

                if (ImGui::IsItemHovered())
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);

                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                    ImGui::SetTooltip("%s", tooltip);

                // Draw indicator of current active view (Visual Studio Code style)
                if (this->uiState == newState)
                {
                    ImDrawList* drawList = ImGui::GetWindowDrawList();

                    const float indicatorRectWidth  = 6.0f;
                    const ImVec2 indicatorRectBegin = ImVec2(0, buttonPos.y);
                    const ImVec2 indicatorRectEnd   = ImVec2(0 + indicatorRectWidth, buttonPos.y + 40.0f);
                    drawList->AddRectFilled(indicatorRectBegin, indicatorRectEnd, IM_COL32(0x60, 0xa0, 0xf3, 0xff));  // Indicator color: #aaaaaa
                }

                ImGui::Dummy(ImVec2(0, 4));
            };

            const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_ChildBg];
            const ImVec4 btnOrigColor = ImGui::GetStyle().Colors[ImGuiCol_Button];    // Use original default color for list's hover color
            ImGui::PushStyleColor(ImGuiCol_Button, bg);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnOrigColor);

            uiSwitchButton("\uf1c7", pUIFiles, "Files");
            uiSwitchButton("\uf0ae", pUIActions, "Actions");
            uiSwitchButton("\uf1da", pUITasks,   "Tasks");
            uiSwitchButton("\uf1ea", pUILog,     "Log");

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
            case pUITasks:
                UI_Tasks();
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

    // Fire a deferred startup warning if loadPreferences() silently reset
    // the saved filename template (ImGuiNotify isn't available before the
    // first rendered frame, so the warning is stored as a flag).
    if (preferences.filenameTemplateWasResetOnLoad)
    {
        preferences.filenameTemplateWasResetOnLoad = false;
        LOG_WARNF("Config", "Startup: filename template in preferences was invalid — reset to default");
        ImGui::InsertNotification({ImGuiToastType::Warning, 8000,
            "Saved filename template was invalid and has been reset to default."});
    }

    // Render ImGuiNotify notifications
    // NOTICE: Even though in ImGui namespace, ImGuiNotify is a third-party library!
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 2.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.14f, 0.15f, 1.00f));

    ImGui::RenderNotifications();  // ← Core

    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(1);
}

int main()
{
    ManifoldApp app;
    app.mainLoop();
}
