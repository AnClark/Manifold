#include "Application.hpp"

#include "imgui.h"
#include "nfd.hpp"

#include "../fonts/DroidSans.hpp"
#include "../fonts/DroidSansFallback.hpp"

#include "sleep_worker.h"
#include "sndfile_worker.h"

#include <windows.h>

using namespace std::chrono_literals;

class TestApp : public ImGuiApplication
{
    // Load worker class
    SleepWorker sleep_worker;
    SndFileWorker sndfile_worker;

public:    
    TestApp(const char* title) : ImGuiApplication(title)
    {
    }

    void onInit() override
    {
        // Initialize NFD
        NFD_Init();

        // Load font with Chinese support
        // Add DroidSans first, then merge DroidSansFallback into DroidSans.
        ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(DroidSansFont_compressed_data, DroidSansFont_compressed_size, 16.0f, nullptr);
        
        ImFontConfig config;
        config.MergeMode = true;
        ImGui::GetIO().Fonts->AddFontFromMemoryCompressedTTF(DroidSansFallbackFont_compressed_data, DroidSansFallbackFont_compressed_size, 16.0f, &config);
    }

    void onTerminate() override
    {
        // Cleanup NFD
        NFD_Quit();
    }

    void onImGuiDisplay() override
    {
        if (ImGui::Begin("Test Threaded Sleep"))
        {
            ImGui::Text("Hello ImGui!");

            if (ImGui::Button("Sleep! for 5 seconds! (Blocking!)"))
            {
                std::this_thread::sleep_for(5s);
            }


            ImGui::BeginDisabled(sleep_worker.shouldDoSleepNow);
            if (ImGui::Button("Sleep! for 5 seconds! (Non-blocking!)"))
            {
                sleep_worker.shouldDoSleepNow = true;
            }
            ImGui::EndDisabled();

            ImGui::Text(sleep_worker.shouldDoSleepNow ? "Sleeping for 5s" : "Not sleeping");

            ImGui::End();
        }

        if (ImGui::Begin("Test Load Sound File"))
        {
            
            if (ImGui::Button("Load ONE sound file (巫启贤)"))
            {
                std::scoped_lock<std::mutex> fetch_parsed_file_list_mutex(sndfile_worker.parsedFileListMutex);

                sndfile_worker.parsedFileList.clear();
                sndfile_worker.addFile(R"(Z:\无损音乐\巫启贤1987-年轻的心[台湾][WAV整轨]\巫启贤.-.[年轻的心](1987)[WAV].flac)");
            }

            if (ImGui::Button("Load ONE sound file..."))
            {
                NFD::UniquePathU8 outPath;
                nfdu8filteritem_t filters[] = {
                    { "Audio Files", "wav,flac,mp3,ogg,aiff" },
                    { "All Files",   "*" }
                };
                nfdresult_t result = NFD::OpenDialog(outPath, filters, 2);
                if (result == NFD_OKAY)
                {
                    std::string nfd_open_path = outPath.get();
                    if (!nfd_open_path.empty())
                    {
                        std::scoped_lock<std::mutex> fetch_parsed_file_list_mutex(sndfile_worker.parsedFileListMutex);

                        sndfile_worker.parsedFileList.clear();
                        sndfile_worker.addFile(nfd_open_path.c_str());                            
                    }
                }
                else if (result == NFD_ERROR)
                {
                    // TODO: Use Dear ImGui's message box
                    MessageBoxA(nullptr, NFD::GetError(), "Load sound file failed", MB_OK | MB_ICONERROR);
                }
            }

            {
                std::scoped_lock<std::mutex> fetch_parsed_file_list_mutex(sndfile_worker.parsedFileListMutex);

                if (!sndfile_worker.parsedFileList.empty())
                {
                    SF_INFO& info = sndfile_worker.parsedFileList[0].info;
                    ImGui::Text("Parsed ONE file:");
                    ImGui::BulletText("File Name: %s", sndfile_worker.parsedFileList[0].filePath.c_str());
                    ImGui::BulletText("Frames: %lld", info.frames);
                    ImGui::BulletText("Sample rate: %d", info.samplerate);
                    ImGui::BulletText("Channels: %d", info.channels);
                }
                else
                {
                    ImGui::Text("No file parsed yet.");
                }
            }

            ImGui::End();
        }

        if (ImGui::Begin("Test NFD"))
        {
            static std::string nfd_open_path;
            static std::vector<std::string> nfd_open_multiple_paths;
            static std::string nfd_save_path;
            static std::string nfd_folder_path;
            static std::string nfd_last_error;

            // --- Open single file ---
            if (ImGui::Button("Open File..."))
            {
                NFD::UniquePathU8 outPath;
                nfdu8filteritem_t filters[] = {
                    { "Audio Files", "wav,flac,mp3,ogg,aiff" },
                    { "All Files",   "*" }
                };
                nfdresult_t result = NFD::OpenDialog(outPath, filters, 2);
                if (result == NFD_OKAY)
                {
                    nfd_open_path = outPath.get();
                    nfd_last_error.clear();
                }
                else if (result == NFD_ERROR)
                {
                    nfd_last_error = NFD::GetError();
                }
            }
            if (!nfd_open_path.empty())
                ImGui::TextWrapped("Opened: %s", nfd_open_path.c_str());

            ImGui::Separator();

            // --- Open multiple files ---
            if (ImGui::Button("Open Multiple Files..."))
            {
                NFD::UniquePathSet outPaths;
                nfdu8filteritem_t filters[] = {
                    { "Audio Files", "wav,flac,mp3,ogg,aiff" },
                    { "All Files",   "*" }
                };
                nfdresult_t result = NFD::OpenDialogMultiple(outPaths, filters, 2);
                if (result == NFD_OKAY)
                {
                    nfd_open_multiple_paths.clear();
                    nfdpathsetsize_t count = 0;
                    NFD::PathSet::Count(outPaths, count);
                    for (nfdpathsetsize_t i = 0; i < count; ++i)
                    {
                        NFD::UniquePathSetPathU8 path;
                        NFD::PathSet::GetPath(outPaths, i, path);
                        nfd_open_multiple_paths.push_back(path.get());
                    }
                    nfd_last_error.clear();
                }
                else if (result == NFD_ERROR)
                {
                    nfd_last_error = NFD::GetError();
                }
            }
            if (!nfd_open_multiple_paths.empty())
            {
                ImGui::Text("Opened %zu file(s):", nfd_open_multiple_paths.size());
                for (const auto& p : nfd_open_multiple_paths)
                    ImGui::BulletText("%s", p.c_str());
            }

            ImGui::Separator();

            // --- Save dialog ---
            if (ImGui::Button("Save File..."))
            {
                NFD::UniquePathU8 outPath;
                nfdu8filteritem_t filters[] = {
                    { "Wave Audio", "wav"  },
                    { "FLAC Audio", "flac" }
                };
                nfdresult_t result = NFD::SaveDialog(outPath, filters, 2, nullptr, "output.wav");
                if (result == NFD_OKAY)
                {
                    nfd_save_path = outPath.get();
                    nfd_last_error.clear();
                }
                else if (result == NFD_ERROR)
                {
                    nfd_last_error = NFD::GetError();
                }
            }
            if (!nfd_save_path.empty())
                ImGui::TextWrapped("Save to: %s", nfd_save_path.c_str());

            ImGui::Separator();

            // --- Pick folder ---
            if (ImGui::Button("Pick Folder..."))
            {
                NFD::UniquePathU8 outPath;
                nfdresult_t result = NFD::PickFolder(outPath);
                if (result == NFD_OKAY)
                {
                    nfd_folder_path = outPath.get();
                    nfd_last_error.clear();
                }
                else if (result == NFD_ERROR)
                {
                    nfd_last_error = NFD::GetError();
                }
            }
            if (!nfd_folder_path.empty())
                ImGui::TextWrapped("Folder: %s", nfd_folder_path.c_str());

            // --- Error display ---
            if (!nfd_last_error.empty())
            {
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
                ImGui::TextWrapped("Error: %s", nfd_last_error.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::End();
        }
    }
};

int main()
{
    TestApp test_app("Test Application with ImGuiApplication class!");
    test_app.mainLoop();
}
