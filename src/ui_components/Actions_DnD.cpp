#include "Actions.hpp"
#include "Main.hpp"

#include "ImGuiNotify_MOD.hpp"

#include <filesystem>

void UIComponents_Actions::system_DropHandler(int count, const char** paths)
{
    auto& self = app->uiFiles;

    LOG_DEBUGF("Actions", "DnD: Droped %d path(s). Manifold only accepts the first path here.", count);

    if (!paths && !paths[0])
    {
        LOG_FATAL("Actions", "DnD: GLFW Drop Handler misbehaves. Cannot detect any path");
        return;
    }
    else
    {
        LOG_DEBUGF("Actions", "DnD: Get path: %s", paths[0]);
    }

    // Avoid overwritting opening dialog or received path
    if (pendingDialog != PendingDialog::Null || ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId))
        return;

    this->dndReceivedPath = paths[0];
    const auto inputPath = std::filesystem::u8path(this->dndReceivedPath);
    if (std::filesystem::is_regular_file(inputPath))
    {
        // NOTE: Popups are not accessible here. Directly invoking ImGui::OpenPopup() will crash the program.
        //       Use flag to open popups instead.
        this->pendingDialog = PendingDialog::LoadNodeChainConfirm;
    }
    else if (std::filesystem::is_directory(inputPath))
    {
        this->pendingDialog = PendingDialog::SetOutputFolderConfirm;
    }
    else
    {
        LOG_ERRORF("Actions", "DnD: Dragged file is not a regular file: %s");
        ImGui::InsertNotification({ImGuiToastType::Error,
                            5000, 
                                "Unrecognized path detected. Please drag a Node Chain file here."});
    }
}
