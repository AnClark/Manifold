#include "Files.hpp"
#include "Main.hpp"

#include <imgui.h>

#include <algorithm>

void UIComponents_Files::subroutine_TableSortSpecs()
{
    // Process sorting specs
    // NOTE: SpecsDirty is set to true when the sort specs change, which is what we are
    //       waiting for here to sort again.
    if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
    {
        if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0)
        {
            std::scoped_lock<std::mutex> guard(app->sndFileListMutex);

            const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
            bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);

            std::stable_sort(app->sndFileList.begin(), app->sndFileList.end(),
                [&](const std::shared_ptr<SndFileInfo>& a, const std::shared_ptr<SndFileInfo>& b)
                {
                    switch (spec.ColumnIndex)
                    {
                        case 0: // File Name
                            return ascending ? a->fileNameBase < b->fileNameBase
                                                : a->fileNameBase > b->fileNameBase;
                        case 1: // Bit Depth
                        {
                            std::string da(a->getBitDepth()), db(b->getBitDepth());
                            return ascending ? da < db : da > db;
                        }
                        case 2: // Duration
                            return ascending ? a->getDurationSeconds() < b->getDurationSeconds()
                                                : a->getDurationSeconds() > b->getDurationSeconds();
                        case 3: // Sample Count
                            return ascending ? a->info.frames < b->info.frames
                                                : a->info.frames > b->info.frames;
                        case 4: // Channels
                            return ascending ? a->info.channels < b->info.channels
                                                : a->info.channels > b->info.channels;
                        case 5: // Sample Rate
                            return ascending ? a->info.samplerate < b->info.samplerate
                                                : a->info.samplerate > b->info.samplerate;
                        case 6: // DC Offset
                            return ascending ? a->maxDcOffset < b->maxDcOffset
                                                : a->maxDcOffset > b->maxDcOffset;
                        case 7: // LUFS-I
                            return ascending ? a->lufsI < b->lufsI
                                                : a->lufsI > b->lufsI;
                        case 8: // Sample Peak
                            return ascending ? a->maxSamplePeak_dBFS < b->maxSamplePeak_dBFS
                                                : a->maxSamplePeak_dBFS > b->maxSamplePeak_dBFS;
                        default:
                            return false;
                    }
                });

            app->lastClickedIndex = -1; // Reset range selection anchor after sorting
            sortSpecs->SpecsDirty = false;
        }
    }
}

void UIComponents_Files::subroutine_HandleShortcutKeys(bool& pendingOpenRemoveConfirm)
{
    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A))
    {
        // Ctrl+A: Select all files
        for (auto& file : app->sndFileList)
            file->selected = true;
    }
    else if (ImGui::Shortcut(ImGuiKey_Delete) || ImGui::Shortcut(ImGuiKey_Backspace))
    {
        // Del / Backspace: Remove selected files
        // NOTE: OpenPopup must be called from the same window context as BeginPopupModal.
        //       We're inside a nested child window here, so defer to the outer scope via flag.
        //       Backspace key is compatible with Mac (no dedicated "Del" key there).
        pendingOpenRemoveConfirm = true;
    }
}

void UIComponents_Files::contextMenu_FileOperations(int selectedFileCount)
{
    // NOTE: Using ImGui's shortcut function ImGui::BeginPopupContextItem().
    //       It creates menu, and registers right-click action in one functon call.
    if (ImGui::BeginPopupContextItem("##f_ctx"))
    {
        if (ImGui::MenuItem(selectedFileCount > 1 ? "Play the first selected file" : "Play selected file"))
            button_PlaySelectedFile(true);

        if (ImGui::MenuItem("Remove..."))
            button_RemoveSelectedFiles(selectedFileCount);

        ImGui::EndPopup();
    }
}
