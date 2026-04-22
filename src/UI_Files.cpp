#include "Main.hpp"

#include "imgui.h"
#include "nfd.hpp"

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
    if (ImGui::Begin("Files"))
    {
        ImGui::Button("Load single file"); ImGui::SameLine();
        if (ImGui::Button("Add Multiple Files..."))
        {
            detectedDuplicateCount = 0;

            NFD::UniquePathSet outPaths;
            nfdu8filteritem_t filters[] = {
                { "Audio Files", "wav,flac,mp3,ogg,aiff" },
                { "All Files",   "*" }
            };
            nfdresult_t result = NFD::OpenDialogMultiple(outPaths, filters, 2);
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
                        newFilePtr->fileName.assign(path.get());
                        sndFileList.push_back(newFilePtr);
                        sndFilePathSet.insert(std::move(pathKey));

                        sndFileWorker.addFile(newFilePtr);
                        ebur128Worker.addFile(newFilePtr);
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

                // 标记取消并从路径集合中删除对应 key
                for (const auto& f : sndFileList)
                    if (f->selected)
                    {
                        f->cancelled = true;
                        sndFilePathSet.erase(makePathKey(f->fileName.c_str()));
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

        ImGui::SameLine();

        if (detectedDuplicateCount > 0)
        {
            ImGui::TextWrapped("Detected %d duplicate file(s) skipped.", detectedDuplicateCount);
        }

        // Insert "Details" view here
        {
            ImGui::Separator();

            // 表格标志：可排序、充满宽度、无内部边框、可滚动
            static ImGuiTableFlags flags = 
                ImGuiTableFlags_Sortable |           // 允许排序
                ImGuiTableFlags_RowBg |              // 行背景交替颜色
                ImGuiTableFlags_SizingStretchProp |  // 列宽充满窗口
                ImGuiTableFlags_ScrollY;             // 垂直滚动

            if (ImGui::BeginTable("FileDetailsTable", 6, flags))
            {
                // 设置列
                ImGui::TableSetupColumn("File Name", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("Sample Count", ImGuiTableColumnFlags_WidthFixed, 120.0f);
                ImGui::TableSetupColumn("Channels", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("Sample Rate", ImGuiTableColumnFlags_WidthFixed, 100.0f);
                ImGui::TableSetupColumn("LUFS-I", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                ImGui::TableSetupColumn("True Peak", ImGuiTableColumnFlags_WidthFixed, 90.0f);
                ImGui::TableHeadersRow();

                // 处理排序（如果需要）
                if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
                {
                    if (sortSpecs->SpecsDirty)
                    {
                        // 这里可以添加实际的排序逻辑
                        // 例如：std::sort(items, items + 10, [&](const FileItem& a, const FileItem& b) { ... });
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

                    if (ImGui::Selectable(sndFileList[i]->fileName.c_str(), sndFileList[i]->selected, selectableFlags))
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
                    ImGui::PopID();
                    
                    // 第二列：Sample Count
                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%lld", sndFileList[i]->info.frames);
                    
                    // 第三列：Channels
                    ImGui::TableSetColumnIndex(2);
                    ImGui::Text("%d", sndFileList[i]->info.channels);
                    
                    // 第四列：Sample Rate
                    ImGui::TableSetColumnIndex(3);
                    ImGui::Text("%d Hz", sndFileList[i]->info.samplerate);
                    
                    // 第五列：LUFS-I
                    ImGui::TableSetColumnIndex(4);
                    ImGui::Text(sndFileList[i]->isR128ParsedOK ? "%.1f dB" : "---", sndFileList[i]->lufsI);
                    
                    // 第六列：True Peak
                    ImGui::TableSetColumnIndex(5);
                    ImGui::Text(sndFileList[i]->isR128ParsedOK ? "%.1f dB" : "---", sndFileList[i]->maxTruePeak_dBTP);
                }
                
                ImGui::EndTable();
            }
        }        
    }
    ImGui::End();
}
