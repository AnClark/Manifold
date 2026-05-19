#include "Files.hpp"
#include "Main.hpp"

#include <imgui.h>
#include "utils/NFDIncludes.h"  // IWYU Pragma: keep
#include "ImGuiNotify_MOD.hpp"

#include <algorithm>

// Returns a normalized path key for duplicate detection: resolves the path and lowercases it on Windows (paths are case-insensitive)
static std::string makePathKey(const char* rawPath)
{
    // weakly_canonical: resolves .., . and redundant separators without requiring the file to be fully accessible
    std::string key = std::filesystem::weakly_canonical(rawPath).string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}

void UIComponents_Files::button_AddMultipleFiles()
{
    if (ImGui::Button("Add Multiple Files..."))
    {
        app->detectedDuplicateCount = 0;

        NFD::UniquePathSet outPaths;
        nfdu8filteritem_t filters[] = {
            { "Audio Files", "wav,flac,mp3,ogg,aiff,caf" },
            { "All Files",   "*" }
        };

        // Pass in the parent window handle: On Windows, the parent window will be automatically disabled while the file dialog is open,
        // preventing users from accidentally interacting with the main interface while the file dialog is open.
        nfdwindowhandle_t parentWindow = {};
        NFD_GetNativeWindowFromGLFWWindow(app->getWindow(), &parentWindow);

        nfdresult_t result = NFD::OpenDialogMultiple(outPaths, filters, 2, nullptr, parentWindow);
        if (result == NFD_OKAY)
        {
            nfdpathsetsize_t count = 0;
            NFD::PathSet::Count(outPaths, count);

            // NOTE:
            // Pre-expand the vector to prevent push_back from triggering a memory reallocation,
            // which would cause the pointers already stored in the worker queue to become invalid (dangling pointers).
            {
                std::scoped_lock<std::mutex> sndFileListGuard(app->sndFileListMutex);
                app->sndFileList.reserve(app->sndFileList.size() + count);
            }

            for (nfdpathsetsize_t i = 0; i < count; ++i)
            {
                NFD::UniquePathSetPathU8 path;
                NFD::PathSet::GetPath(outPaths, i, path);

                std::string pathKey = makePathKey(path.get());

                {
                    std::scoped_lock<std::mutex> sndFileListGuard(app->sndFileListMutex);

                    // O(1) duplicate detection: skip if path already exists
                    if (app->sndFilePathSet.count(pathKey))
                    {
                        app->detectedDuplicateCount++;
                        continue;
                    }

                    auto newFilePtr = std::make_shared<SndFileInfo>();
                    newFilePtr->updateFilePath(path.get());
                    app->sndFileList.push_back(newFilePtr);
                    app->sndFilePathSet.insert(std::move(pathKey));

                    app->sndFileWorker.addFile(newFilePtr);
                    app->ebur128Worker.addFile(newFilePtr);
                    app->dcOffsetWorker.addFile(newFilePtr);
                }
            }
            app->NFDLastError.clear();
        }
        else if (result == NFD_ERROR)
        {
            app->NFDLastError = NFD::GetError();

            const char* errMsgTemplate = "Failed when loading file dialog: %s";
            LOG_ERRORF("Files", errMsgTemplate, app->NFDLastError.c_str());
            ImGui::InsertNotification({ImGuiToastType::Error, 5000, errMsgTemplate, app->NFDLastError.c_str()});
        }
    }
}

void UIComponents_Files::button_RemoveSelectedFiles(int selectedCount)
{
    ImGui::BeginDisabled(selectedCount == 0);
    if (ImGui::Button("Remove selected file(s)"))
        ImGui::OpenPopup("##remove_confirm");
    ImGui::EndDisabled();
}

void UIComponents_Files::popup_ConfirmRemoveSelectedFiles(int selectedCount)
{
    if (ImGui::BeginPopupModal("##remove_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Remove %d selected file(s) from the list?", selectedCount);
        ImGui::Separator();

        if (ImGui::Button("Remove", ImVec2(120, 0)))
        {
            std::scoped_lock<std::mutex> guard(app->sndFileListMutex);

            // If the currently playing file is among those being removed, stop playback
            if (app->currentPlayingFile && app->currentPlayingFile->selected)
            {
                app->audioPlayer.cleanUp();
                app->currentPlayingFile.reset(); // reset to nullptr after stopping playback to avoid a dangling pointer
            }

            // Mark files for removal and erase their corresponding keys from the path set
            for (const auto& f : app->sndFileList)
                if (f->selected)
                {
                    f->aboutToBeRemoved = true;
                    app->sndFilePathSet.erase(makePathKey(f->filePath.c_str()));
                }

            // Remove selected entries from the list (the worker decides when to actually free the object after shared_ptr destruction)
            app->sndFileList.erase(
                std::remove_if(app->sndFileList.begin(), app->sndFileList.end(),
                    [](const std::shared_ptr<SndFileInfo>& f) { return f->selected; }),
                app->sndFileList.end()
            );

            app->lastClickedIndex = -1;
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}
