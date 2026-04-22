#include "Main.hpp"

#include "imgui.h"
#include "nfd.h"

#include "../fonts/DroidSans.hpp"
#include "../fonts/DroidSansFallback.hpp"

void ManifoldApp::onInit()
{
    // Initialize NFD
    NFD_Init();

    // Load font with Chinese support
    // 1. Add DroidSans first
    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(DroidSansFont_compressed_data, DroidSansFont_compressed_size, 16.0f, nullptr);
    // 2. Merge DroidSansFallback into DroidSans.
    ImFontConfig config;
    config.MergeMode = true;
    ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(DroidSansFallbackFont_compressed_data, DroidSansFallbackFont_compressed_size, 16.0f, &config);
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

    // Cleanup NFD
    NFD_Quit();
}

void ManifoldApp::onImGuiDisplay()
{
    UI_Files();
}

int main()
{
    ManifoldApp app;
    app.mainLoop();
}
