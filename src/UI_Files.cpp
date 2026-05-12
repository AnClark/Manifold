#include "Main.hpp"

#include "imgui.h"
#include "utils/NFDIncludes.h"  // IWYU pragma: keep
#include "ImGuiNotify_MOD.hpp"

#include <algorithm>
#include <filesystem>

// 返回用于重复检测的路径关键字：解析路径并在 Windows 上转为小写（路径不区分大小写）
static std::string makePathKey(const char* rawPath)
{
    // weakly_canonical: 处理 .. / . 和多余分隔符，不要求文件完全可访问
    std::string key = std::filesystem::weakly_canonical(rawPath).string();
#ifdef _WIN32
    std::transform(key.begin(), key.end(), key.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
    return key;
}

void ManifoldApp::UI_Files()
{
    if (ImGui::BeginChild("Files"))
    {
        //
        // TOOLBAR
        //
        ImGui::BeginGroup();
        {
            ImGui::BeginDisabled(lastClickedIndex <= -1 || !sndFileList[lastClickedIndex]->errorMsg.empty());
            if (ImGui::Button("Play selected file"))
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

            if (audioPlayer.hasError())
            {
                char errorMsg[512];
                snprintf(errorMsg, 512, "Failed to play audio.\n%s", audioPlayer.getErrorMsg());

                LOG_ERRORF("Files", errorMsg);
                ImGui::InsertNotification({ImGuiToastType::Error, 5000, "%s", errorMsg});

                // Now we have reported and logged error message. Remember to clear it.
                audioPlayer.clearErrorMsg();
            }

            ImGui::SameLine();

            // Transport control
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

            ImGui::BeginDisabled(!currentPlayingFile);
            if (ImGui::Button("Stop"))
            {
                audioPlayer.stop();
                currentPlayingFile.reset(); // reset currentPlayingFile to nullptr after stopping playback to avoid dangling pointer
            }
            ImGui::EndDisabled();

            ImGui::SameLine();

            if (ImGui::Button("Add Multiple Files..."))
            {
                detectedDuplicateCount = 0;

                NFD::UniquePathSet outPaths;
                nfdu8filteritem_t filters[] = {
                    { "Audio Files", "wav,flac,mp3,ogg,aiff" },
                    { "All Files",   "*" }
                };

                // Pass in the parent window handle: On Windows, the parent window will be automatically disabled while the file dialog is open,
                // preventing users from accidentally interacting with the main interface while the file dialog is open.
                nfdwindowhandle_t parentWindow = {};
                NFD_GetNativeWindowFromGLFWWindow(getWindow(), &parentWindow);

                nfdresult_t result = NFD::OpenDialogMultiple(outPaths, filters, 2, nullptr, parentWindow);
                if (result == NFD_OKAY)
                {
                    nfdpathsetsize_t count = 0;
                    NFD::PathSet::Count(outPaths, count);

                    // NOTICE:
                    // Pre-expand the vector to prevent push_back from triggering a memory reallocation,
                    // which would cause the pointers already stored in the worker queue to become invalid (dangling pointers).
                    {
                        std::scoped_lock<std::mutex> sndFileListGuard(sndFileListMutex);
                        sndFileList.reserve(sndFileList.size() + count);
                    }

                    for (nfdpathsetsize_t i = 0; i < count; ++i)
                    {
                        NFD::UniquePathSetPathU8 path;
                        NFD::PathSet::GetPath(outPaths, i, path);

                        std::string pathKey = makePathKey(path.get());

                        {
                            std::scoped_lock<std::mutex> sndFileListGuard(sndFileListMutex);

                            // O(1) 重复检测：路径已存在则跳过
                            if (sndFilePathSet.count(pathKey))
                            {
                                detectedDuplicateCount++;
                                continue;
                            }

                            auto newFilePtr = std::make_shared<SndFileInfo>();
                            newFilePtr->updateFilePath(path.get());
                            sndFileList.push_back(newFilePtr);
                            sndFilePathSet.insert(std::move(pathKey));

                            sndFileWorker.addFile(newFilePtr);
                            ebur128Worker.addFile(newFilePtr);
                            dcOffsetWorker.addFile(newFilePtr);
                        }
                    }
                    NFDLastError.clear();
                }
                else if (result == NFD_ERROR)
                {
                    // TODO: Use message box to show errors
                    NFDLastError = NFD::GetError();
                }
            }
            ImGui::SameLine();

            // 统计已选中数量（selected 仅由主线程修改，无需加锁）
            int selectedCount = 0;
            for (const auto& f : sndFileList)
                if (f->selected) selectedCount++;

            ImGui::BeginDisabled(selectedCount == 0);
            if (ImGui::Button("Remove selected file(s)"))
                ImGui::OpenPopup("##remove_confirm");
            ImGui::EndDisabled();


            // 确认对话框
            if (ImGui::BeginPopupModal("##remove_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::Text("Remove %d selected file(s) from the list?", selectedCount);
                ImGui::Separator();

                if (ImGui::Button("Remove", ImVec2(120, 0)))
                {
                    std::scoped_lock<std::mutex> guard(sndFileListMutex);

                    // 若正在播放的音频是被删除的文件之一，则停止播放
                    if (currentPlayingFile && currentPlayingFile->selected)
                    {
                        audioPlayer.cleanUp();
                        currentPlayingFile.reset(); // reset currentPlayingFile to nullptr after stopping playback to avoid dangling pointer
                    }

                    // 标记删除，并从路径集合中删除对应 key
                    for (const auto& f : sndFileList)
                        if (f->selected)
                        {
                            f->aboutToBeRemoved = true;
                            sndFilePathSet.erase(makePathKey(f->filePath.c_str()));
                        }

                    // 从列表中移除已选中项（shared_ptr 析构后对象由 worker 决定何时真正释放）
                    sndFileList.erase(
                        std::remove_if(sndFileList.begin(), sndFileList.end(),
                            [](const std::shared_ptr<SndFileInfo>& f) { return f->selected; }),
                        sndFileList.end()
                    );

                    lastClickedIndex = -1;
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(120, 0)))
                    ImGui::CloseCurrentPopup();

                ImGui::EndPopup();
            }
        }
        ImGui::EndGroup();

        if (detectedDuplicateCount > 0)
        {
            LOG_WARNF("Files", "User attempted to add %d duplicate file(s), which were skipped.", detectedDuplicateCount);
            ImGui::InsertNotification({ImGuiToastType::Warning, 5000, "Detected %d duplicate files(s). Skipped.", detectedDuplicateCount});

            // Remember to reset the counter after reporting to avoid showing stale warnings on next additions
            detectedDuplicateCount = 0;
        }

        //
        // FILE DETAILS view
        //
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

                // 显示数据行
                for (int i = 0; i < sndFileList.size(); i++)
                {
                    ImGui::TableNextRow();
                    
                    // 第一列：File Name（可选择）
                    ImGui::TableSetColumnIndex(0);
                    ImGuiSelectableFlags selectableFlags = 
                        ImGuiSelectableFlags_SpanAllColumns |    // 选择跨越所有列
                        ImGuiSelectableFlags_AllowOverlap;       // 允许其他项重叠
                    
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

                    ImGui::PopID();
                    
                    // 第二列：Bit Depth
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%s", sndFileList[i]->isParseOK ? sndFileList[i]->getBitDepth() : "---");

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
                    ImGui::TableSetColumnIndex(7);
                    ImGui::Text(sndFileList[i]->isR128ParsedOK ? "%.1f dB" : "---", sndFileList[i]->lufsI);
                    
                    // 第九列：Sample Peak
                    ImGui::TableSetColumnIndex(8);
                    ImGui::Text(sndFileList[i]->isR128ParsedOK ? "%.1f dB" : "---", sndFileList[i]->maxSamplePeak_dBFS);
                }
                
                ImGui::EndTable();
            }

            ImGui::EndChild();
        }        
    }
    ImGui::EndChild();
}
