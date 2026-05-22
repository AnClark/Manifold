#include "Files.hpp"
#include "Main.hpp"

#include <imgui.h>
#include "utils/NFDIncludes.h"  // IWYU Pragma: keep
#include "ImGuiNotify_MOD.hpp"

#include <algorithm>
#include <unordered_set>

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

// Ingests a batch of raw file paths: deduplication, SndFileInfo creation, and worker submission.
void UIComponents_Files::_ingestPaths(const std::vector<std::string>& paths)
{
    std::scoped_lock<std::mutex> guard(app->sndFileListMutex);

    // NOTE:
    // Pre-expand the vector to prevent push_back from triggering a memory reallocation,
    // which would cause the pointers already stored in the worker queue to become invalid (dangling pointers).
    app->sndFileList.reserve(app->sndFileList.size() + paths.size());

    for (const auto& rawPath : paths)
    {
        std::string pathKey = makePathKey(rawPath.c_str());

        // O(1) duplicate detection: skip if path already exists
        if (app->sndFilePathSet.count(pathKey))
        {
            app->detectedDuplicateCount++;
            continue;
        }

        auto newFilePtr = std::make_shared<SndFileInfo>();
        newFilePtr->updateFilePath(rawPath.c_str());
        app->sndFileList.push_back(newFilePtr);
        app->sndFilePathSet.insert(std::move(pathKey));

        app->sndFileWorker.addFile(newFilePtr);
#ifdef ENABLE_PARALLEL_ANALYZING
        app->analyzerDispatcher.addFile(newFilePtr);
#else
        app->ebur128Worker.addFile(newFilePtr);
        app->dcOffsetWorker.addFile(newFilePtr);
#endif
    }
}

int UIComponents_Files::_findAudioFiles(const char* pickedPath, std::vector<std::string>& foundAudioFilePaths, bool clearContainer)
{
    static const std::unordered_set<std::string> supportedExts = {
        ".wav", ".flac", ".mp3", ".ogg", ".aiff", ".caf"
    };

    int foundFilesCount = 0;

    if (clearContainer)
        foundAudioFilePaths.clear();

    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(pickedPath, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        std::string ext = entry.path().extension().string();
        // Lowercase extension for case-insensitive comparison
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (supportedExts.count(ext))
        {
            foundAudioFilePaths.push_back(entry.path().string());
            foundFilesCount++;
        }
    }

    return foundFilesCount;
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

            std::vector<std::string> paths;
            paths.reserve(count);
            for (nfdpathsetsize_t i = 0; i < count; ++i)
            {
                NFD::UniquePathSetPathU8 path;
                NFD::PathSet::GetPath(outPaths, i, path);
                paths.emplace_back(path.get());
            }

            _ingestPaths(paths);
            LOG_INFOF("Files", "Added %d audio files to list.", paths.size());
            if (paths.size())
            {
                if (app->detectedDuplicateCount > 0)
                    subroutine_WarnAboutDuplicateFiles(paths.size());
                else           
                    ImGui::InsertNotification({ImGuiToastType::Success, 5000, "Added %d audio files to list.", paths.size()});
            }
            else
                ImGui::InsertNotification({ImGuiToastType::Warning, 5000, "No file added to list.", paths.size()});

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

void UIComponents_Files::button_AddFolder()
{
    if (ImGui::Button("Add Folder..."))
    {
        app->detectedDuplicateCount = 0;

        nfdu8char_t* pickedPath = nullptr;

        nfdwindowhandle_t parentWindow = {};
        NFD_GetNativeWindowFromGLFWWindow(app->getWindow(), &parentWindow);

        nfdresult_t result = NFD::PickFolder(pickedPath, nullptr, parentWindow);
        if (result == NFD_OKAY)
        {
            std::vector<std::string> foundPaths;
            _findAudioFiles(pickedPath, foundPaths, true);

            _ingestPaths(foundPaths);
            LOG_INFOF("Files", "Added %d audio files from specified folder (%s).", foundPaths.size(), pickedPath);
            if (foundPaths.size())
            {
                if (app->detectedDuplicateCount > 0)
                    subroutine_WarnAboutDuplicateFiles(foundPaths.size());
                else
                    ImGui::InsertNotification({ImGuiToastType::Success, 5000, "Added %d audio files from specified folder.", foundPaths.size()});
            }
            else
                ImGui::InsertNotification({ImGuiToastType::Warning, 5000, "No file added from specified folder.\n(Maybe no media files in folder, or failed to open directory?)", foundPaths.size()});

            NFD::FreePath(pickedPath);

            app->NFDLastError.clear();
        }
        else if (result == NFD_ERROR)
        {
            app->NFDLastError = NFD::GetError();

            const char* errMsgTemplate = "Failed when loading folder dialog: %s";
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

        if (ImGui::Shortcut(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }
}

void UIComponents_Files::subroutine_WarnAboutDuplicateFiles(int addedFiles)
{
    if (app->detectedDuplicateCount > 0)
    {
        LOG_WARNF("Files", "User attempted to add %d duplicate file(s), which were skipped.", app->detectedDuplicateCount);
        ImGui::InsertNotification({ImGuiToastType::Warning, 5000, "Detected %d duplicate file(s) out of %d newly added files. Skipped.", app->detectedDuplicateCount, addedFiles});

        // Remember to reset the counter after reporting to avoid showing stale warnings on next additions
        app->detectedDuplicateCount = 0;
    }
}

void UIComponents_Files::system_DropHandler(int count, const char** paths)
{
    auto& self = app->uiFiles;

    LOG_DEBUGF("Files", "DnD: Droped %d path(s)", count);

    std::vector<std::string> foundPaths;   
    uint32_t s_filesAddedFromFolders = 0;     
    uint32_t s_scannedFolders = 0;

    for (int i = 0; i < count; i++) {
        const auto currentPath = std::filesystem::u8path(paths[i]);
        const bool isDirectory = std::filesystem::is_directory(currentPath);
        LOG_DEBUGF("Files", "DnD:\t- [%s] %s", isDirectory ? "DIR" : "FILE", currentPath.generic_string().c_str());

        if (isDirectory)
        {
            const auto found = self._findAudioFiles(currentPath.string().c_str(), foundPaths);
            s_filesAddedFromFolders += found;
            s_scannedFolders++;
        }
        else
        {
            foundPaths.emplace_back(currentPath.string());
        }
    }

    if (foundPaths.size() > 0)
    {
        self._ingestPaths(foundPaths);

        if (s_scannedFolders)
        {
            if (app->detectedDuplicateCount > 0)
            {
                LOG_DEBUGF("Files", "DnD: Detected duplicated files (maybe in those %d files added from %d folders(s)). See log entry below.", s_filesAddedFromFolders, s_scannedFolders);
                self.subroutine_WarnAboutDuplicateFiles(foundPaths.size());
            }
            else
            {
                LOG_INFOF("Files", "DnD: Added %d audio files to list [including %d files added from %d folder(s)].", foundPaths.size(), s_filesAddedFromFolders, s_scannedFolders);
                ImGui::InsertNotification({ImGuiToastType::Success,
                                    5000,
                                        "Added %d audio files to list\n(including %d from %d folders).",
                                                foundPaths.size(), s_filesAddedFromFolders, s_scannedFolders});
            }
        }
        else
        {
            if (app->detectedDuplicateCount > 0)
            {
                LOG_DEBUGF("Files", "DnD: Detected duplicated files. See log entry below.");
                self.subroutine_WarnAboutDuplicateFiles(foundPaths.size());
            }
            else
            {
                LOG_INFOF("Files", "DnD: Added %d files to list.", foundPaths.size());
                ImGui::InsertNotification({ImGuiToastType::Success,
                                    5000,
                                        "Added %d audio files to list.",
                                                foundPaths.size()});
            }
        }
    }
    else
    {
        LOG_WARNF("Files", "DnD: No files added to list.");
        ImGui::InsertNotification({ImGuiToastType::Warning,
                            5000, 
                                "No file added from specified folder.\n(Maybe no media files in folder, or failed to open directory?)"});
    }
}

