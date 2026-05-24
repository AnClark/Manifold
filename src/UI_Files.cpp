#include "Main.hpp"
#include "config/Config.hpp"

#include "imgui.h"
#include "utils/NFDIncludes.h"  // IWYU pragma: keep
#include "ImGuiNotify_MOD.hpp"

#include <algorithm>
#include <filesystem>

void ManifoldApp::UI_Files()
{
    if (ImGui::BeginChild("Files"))
    {
        // Calculate the count of selected files (`selected` is only modified by main thread, no lock needed)
        int selectedCount = 0;
        for (const auto& f : sndFileList)
            if (f->selected) selectedCount++;

        //
        // TOOLBAR
        //
        ImGui::BeginGroup();
        {
            // Playback controls
            {
                uiFiles.button_PlaySelectedFile();
                uiFiles.subroutine_ReportOnAudioPlayerError();

                ImGui::SameLine();

                uiFiles.button_PauseOrResume();

                ImGui::SameLine();

                uiFiles.button_StopPlaying();                
            }

            ImGui::SameLine(0, 16.0f);

            uiFiles.button_AddMultipleFiles();

            ImGui::SameLine();

            uiFiles.button_AddFolder();

            ImGui::SameLine();

            uiFiles.button_RemoveSelectedFiles(selectedCount);

            ImGui::SameLine();

            uiFiles.button_ExportFileList();

            ImGui::SameLine();

            uiFiles.button_Refresh();
        }
        ImGui::EndGroup();

        //
        // FILE DETAILS view
        //
        bool pendingOpenRemoveConfirm = false;

        if (ImGui::BeginChild("File List With Details"))
        {
            // 表格标志：可排序、充满宽度、无内部边框、可滚动
            static ImGuiTableFlags flags = 
                ImGuiTableFlags_Sortable |           // 允许排序
                ImGuiTableFlags_RowBg |              // 行背景交替颜色
                ImGuiTableFlags_SizingStretchProp |  // 列宽充满窗口
                ImGuiTableFlags_ScrollY;             // 垂直滚动

            if (ImGui::BeginTable("FileDetailsTable", 9, flags))
            {
                // Freeze the first row (header row)
                ImGui::TableSetupScrollFreeze(0, 1);
                // 设置列
                ImGui::TableSetupColumn("File Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Bit Depth", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Duration", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Sample Count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Channels", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Sample Rate", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("DC Offset", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("LUFS-I", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Peak", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();

                // Process sorting specs. (Note that SpecsDirty is set to true when the sort specs change, which is what we are waiting for here to sort again.)
                if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
                {
                    if (sortSpecs->SpecsDirty && sortSpecs->SpecsCount > 0)
                    {
                        std::scoped_lock<std::mutex> guard(sndFileListMutex);

                        const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                        bool ascending = (spec.SortDirection == ImGuiSortDirection_Ascending);

                        std::stable_sort(sndFileList.begin(), sndFileList.end(),
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

                        lastClickedIndex = -1; // Reset range selection anchor after sorting
                        sortSpecs->SpecsDirty = false;
                    }
                }

                // Process shortcut keys
                if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_A))
                {
                    // Ctrl+A: Select all files
                    for (auto& file : sndFileList)
                        file->selected = true;
                }
                else if (ImGui::Shortcut(ImGuiKey_Delete))
                {
                    // Del: Remove selected files
                    // NOTE: OpenPopup must be called from the same window context as BeginPopupModal.
                    //       We're inside a nested child window here, so defer to the outer scope via flag.
                    pendingOpenRemoveConfirm = true;
                }

                // 显示数据行
                for (int i = 0; i < sndFileList.size(); i++)
                {
                    ImGui::TableNextRow();
                    
                    // 第一列：File Name（可选择）
                    ImGui::TableSetColumnIndex(0);
                    ImGuiSelectableFlags selectableFlags = 
                        ImGuiSelectableFlags_SpanAllColumns |    // 选择跨越所有列
                        ImGuiSelectableFlags_AllowOverlap   |    // 允许其他项重叠
                        ImGuiSelectableFlags_AllowDoubleClick;
                    
                    ImGui::PushID(reinterpret_cast<uintptr_t>(sndFileList[i].get()));

                    // Add mutex lock
                    std::scoped_lock<std::mutex> sndFileListGuard(sndFileListMutex);

                    // Snapshot the error state once while holding the lock.
                    // Worker writes isParseOK/errorMsg without this mutex, so the two condition
                    // checks could see different values, causing Push/Pop count mismatch.
                    const bool hasParseError = !sndFileList[i]->isParseOK && !sndFileList[i]->errorMsg.empty();

                    if (hasParseError)
                    {
                        // If parse failure (metadata), set row color to #f8748a
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(0xcc, 0x5f, 0x71, 127));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0xcc + 48, 0x5f + 48, 0x71 + 48, 127));  // ImGui::Selectable uses "Header" for its color definition
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive, IM_COL32(0xcc + 64, 0x5f + 64, 0x71 + 64, 127));
                    }

                    constexpr size_t MAX_LABEL_SIZE = 256 + 1 + 2;
                    char fileNameLabel[MAX_LABEL_SIZE];
                    snprintf(fileNameLabel, MAX_LABEL_SIZE, "%s%s",
                        currentPlayingFile == sndFileList[i] ? (audioPlayer.checkPlaying() ? ICON_FA_PLAY " " : (audioPlayer.checkEOF() ? ICON_FA_STOP "  " : ICON_FA_PAUSE "  ")) : "",
                        sndFileList[i]->fileNameBase.c_str()
                    );

                    if (ImGui::Selectable(fileNameLabel, sndFileList[i]->selected, selectableFlags, ImVec2(0, 20.0f)))
                    {
                        // 单选/多选/范围选择逻辑
                        if (ImGui::GetIO().KeyShift && lastClickedIndex != -1)
                        {
                            // Shift+点击：范围选择
                            int rangeStart = (lastClickedIndex < i) ? lastClickedIndex : i;
                            int rangeEnd = (lastClickedIndex < i) ? i : lastClickedIndex;
                            
                            if (!ImGui::GetIO().KeyCtrl)
                            {
                                // 如果没有按 Ctrl，先清除所有选择
                                for (int j = 0; j < sndFileList.size(); j++)
                                    sndFileList[j]->selected = false;
                            }
                            
                            // 选择范围内的所有项目
                            for (int j = rangeStart; j <= rangeEnd; j++)
                                sndFileList[j]->selected = true;
                        }
                        else if (ImGui::GetIO().KeyCtrl)
                        {
                            // Ctrl+点击：切换当前项的选择状态（多选）
                            sndFileList[i]->selected = !sndFileList[i]->selected;
                            lastClickedIndex = i;
                        }
                        else
                        {
                            // 普通点击：清除其他选择，只选择当前项（单选）
                            for (int j = 0; j < sndFileList.size(); j++)
                                sndFileList[j]->selected = false;
                            sndFileList[i]->selected = true;
                            lastClickedIndex = i;
                        }
                    }

                    if (hasParseError)
                    {
                        // Remember to pop style color first!
                        ImGui::PopStyleColor(2); // ImGuiCol_HeaderHovered

                        // Show a tooltip telling where error happens
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay) && ImGui::BeginItemTooltip())
                        {
                            ImGui::Text("Error while parsing audio file:");
                            ImGui::BulletText("%s", sndFileList[i]->errorMsg.c_str());
                            ImGui::Separator();
                            ImGui::Text("%s", sndFileList[i]->filePath.c_str());
                            ImGui::EndTooltip();
                        }
                    }
                    else
                    {
                        // Show full path as tooltip
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort | ImGuiHoveredFlags_NoSharedDelay) && ImGui::BeginItemTooltip())
                        {
                            ImGui::Text("%s", sndFileList[i]->filePath.c_str());
                            ImGui::EndTooltip();
                        }
                    }

                    // Double click: Play selected file.
                    // NOTE: IsItemHovered() is required here to gate the action to the hovered row only.
                    //       IsMouseDoubleClicked() returns true for the entire frame, so without the
                    //       hover check every row in the list would trigger playback on the same frame,
                    //       causing loadAudioFile()+initDevice() to be called N times and stuttering.
                    if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    {
                        const bool notPlayable = lastClickedIndex <= -1 || !sndFileList[lastClickedIndex]->errorMsg.empty();
                        if (!notPlayable)
                            uiFiles.button_PlaySelectedFile(true);  // param: runAsCommand = true
                    }

                    ImGui::PopID();
                    
                    // 第二列：Bit Depth
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", sndFileList[i]->isParseOK ? sndFileList[i]->getBitDepth() : "---");
                    if (ImGui::IsItemHovered() && sndFileList[i]->isSpecialBitDepthFormat())
                        ImGui::SetTooltip("NOTICE:\nSome codecs (MP3, Vorbis, Opus etc.) have special bit depths,\n"
                                               "So you will only see their codec names here.");

                    // 第三列：Duration
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%s", sndFileList[i]->durationString);

                    // 第四列：Sample Count
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%lld", sndFileList[i]->info.frames);
                    
                    // 第五列：Channels
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text("%d", sndFileList[i]->info.channels);
                    
                    // 第六列：Sample Rate
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text("%d Hz", sndFileList[i]->info.samplerate);

                    // 第七列：DC Offset
                    ImGui::TableSetColumnIndex(6);
                    ImGui::Text(sndFileList[i]->isDcOffsetCalculatedOK ? "%.5f" : "---", sndFileList[i]->maxDcOffset);

                    // 第八列：LUFS-I
                    // NOTE: It is unable to measure LUFS-I for audio shorter than 400ms, so we need to inform users.
                    ImGui::TableSetColumnIndex(7);
                    const bool durationTooShort = (sndFileList[i]->getDurationSeconds() < 0.4);
                    if (durationTooShort)
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(127, 127, 127, 255));
                    ImGui::Text(sndFileList[i]->isR128ParsedOK ? "%.1f dB" : "---", sndFileList[i]->lufsI);
                    if (durationTooShort)
                        ImGui::PopStyleColor();                    
                    if (durationTooShort && ImGui::IsItemHovered() && ImGui::BeginTooltipEx(ImGuiTooltipFlags_OverridePrevious, ImGuiWindowFlags_None))
                    {
                        ImGui::Text("Warning:");
                        ImGui::BulletText("File too short for LUFS-I measurement (< 400 ms), so LUFS-I value is -inf.");
                        ImGui::Separator();
                        ImGui::TextDisabled("This is the characteristic of EBU R128, not a bug of Manifold.");
                        ImGui::EndTooltip();
                    }
                    
                    // 第九列：Sample Peak
                    ImGui::TableSetColumnIndex(8);
                    ImGui::Text(sndFileList[i]->isR128ParsedOK ? "%.1f dB" : "---", sndFileList[i]->maxSamplePeak_dBFS);
                }
                
                ImGui::EndTable();
            }

            ImGui::EndChild();
        }        
        
        //
        // POPUPS
        //

        if (pendingOpenRemoveConfirm && selectedCount > 0)
            ImGui::OpenPopup("##remove_confirm");
        uiFiles.popup_ConfirmRemoveSelectedFiles(selectedCount);
    }
    ImGui::EndChild();
}
