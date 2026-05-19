#pragma once

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
};
