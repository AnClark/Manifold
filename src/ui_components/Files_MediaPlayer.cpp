#include "Files.hpp"
#include "Main.hpp"

#include <imgui.h>
#include "ImGuiNotify_MOD.hpp"

void UIComponents_Files::button_PlaySelectedFile(bool runAsCommand)
{
    auto& audioPlayer = app->audioPlayer;
    auto& lastClickedIndex = app->lastClickedIndex;
    auto& sndFileList = app->sndFileList;
    auto& currentPlayingFile = app->currentPlayingFile;

    ImGui::BeginDisabled(lastClickedIndex <= -1 || !sndFileList[lastClickedIndex]->errorMsg.empty());
    if (runAsCommand || ImGui::Button("Play selected file"))    // NOTE: Evaluate runAsCommand first, to avoid unexpected flashing in list view.
    {
        if (lastClickedIndex >= 0 && lastClickedIndex < sndFileList.size())
        {
            currentPlayingFile = sndFileList[lastClickedIndex];

            audioPlayer.loadAudioFile(currentPlayingFile->filePath.c_str());
            audioPlayer.initDevice();
            audioPlayer.play();
        }

    }
    ImGui::EndDisabled();
}

void UIComponents_Files::button_PauseOrResume()
{
    auto& audioPlayer = app->audioPlayer;
    auto& currentPlayingFile = app->currentPlayingFile;

    ImGui::BeginDisabled(!currentPlayingFile || audioPlayer.checkEOF());
    if (ImGui::Button((!currentPlayingFile || audioPlayer.checkPlaying() || audioPlayer.checkEOF()) ? "Pause" : "Resume", ImVec2(60, 0)))
    {
        if (audioPlayer.checkPlaying())
            audioPlayer.pause();
        else
            audioPlayer.play();
    }
    ImGui::SameLine();
    ImGui::EndDisabled();
}

void UIComponents_Files::button_StopPlaying()
{
    auto& audioPlayer = app->audioPlayer;
    auto& currentPlayingFile = app->currentPlayingFile;

    ImGui::BeginDisabled(!currentPlayingFile);
    if (ImGui::Button("Stop", ImVec2(60, 0)))
    {
        audioPlayer.stop();
        currentPlayingFile.reset(); // reset currentPlayingFile to nullptr after stopping playback to avoid dangling pointer
    }
    ImGui::EndDisabled();
}

void UIComponents_Files::subroutine_ReportOnAudioPlayerError()
{
    auto& audioPlayer = app->audioPlayer;

    if (audioPlayer.hasError())
    {
        char errorMsg[512];
        snprintf(errorMsg, 512, "Failed to play audio.\n%s", audioPlayer.getErrorMsg());

        LOG_ERRORF("Files", errorMsg);
        ImGui::InsertNotification({ImGuiToastType::Error, 5000, "%s", errorMsg});

        // Now we have reported and logged error message. Remember to clear it.
        audioPlayer.clearErrorMsg();
    }
}
