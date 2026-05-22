#pragma once

#include <string>
#include <vector>

// Forward decls.
class ManifoldApp;

class UIComponents_Files
{
public:
    UIComponents_Files(ManifoldApp* app_) : app(app_)
    {}

    void button_PlaySelectedFile();
    void button_PauseOrResume();
    void button_StopPlaying();
    void subroutine_ReportOnAudioPlayerError();

    void button_AddMultipleFiles();
    void button_AddFolder();
    void button_RemoveSelectedFiles(int selectedCount);
    void popup_ConfirmRemoveSelectedFiles(int selectedCount);
    void subroutine_WarnAboutDuplicateFiles(int addedFiles);

    void system_DropHandler(int count, const char** paths);

private:
    ManifoldApp* app;

    // Ingests a batch of raw file paths: deduplication, SndFileInfo creation, and worker submission.
    // Must be called WITHOUT holding sndFileListMutex.
    void _ingestPaths(const std::vector<std::string>& paths);

    // Recursively searches for audio files under the specified path (file or folder).
    // Returns the count of found audio files.
    int _findAudioFiles(const char* pickedPath, std::vector<std::string>& foundAudioFilePaths, bool clearContainer = false);    
};
