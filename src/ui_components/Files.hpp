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

private:
    ManifoldApp* app;

    // Ingests a batch of raw file paths: deduplication, SndFileInfo creation, and worker submission.
    // Must be called WITHOUT holding sndFileListMutex.
    void _ingestPaths(const std::vector<std::string>& paths);
};
