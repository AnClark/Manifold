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
