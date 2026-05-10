#include "Main.hpp"
#include "pipeline/NodeRegistry.hpp"

#include "imgui.h"
#include "nfd.hpp"
#include "utils/TableMinColumnWidth.hpp"
#include "pipeline/base_nodes/DSPNode.hpp"

#include "../fonts/IconFontAwesome5.h"

namespace ImGuiExt
{
    /**
     * @brief ImGui extention: Create footer layout.
     * Invoke this method, then any elements created later will be displayed as "footer" in your window.
     */
    void MakeFooter(float footer_height = -1.0f)
    {
        // ========== Footer: Auto stick-to-bottom logic ==========
        // If specify negative value, estimate the total height needed for the Footer area
        // (separator + text + spacing)
        if (footer_height < 0)
            footer_height = ImGui::GetFrameHeightWithSpacing() + ImGui::GetStyle().ItemSpacing.y;

        // Calculate the remaining space from the current cursor to the bottom of the window
        float spacing = ImGui::GetContentRegionAvail().y - footer_height;

        // If there is still extra space, insert a Dummy to push the Footer to the bottom
        if (spacing > 0.0f)
            ImGui::Dummy(ImVec2(0.0f, spacing));

        // Add a dummy space to make the footer has enough height
        // To let the widgets draw in the right place, current cursor position should be stored,
        // then restore after the dummy space.
        const ImVec2 cursor_pos_backup = ImGui::GetCursorPos();
        ImGui::Dummy(ImVec2(0, footer_height - (footer_height * 0.05f)));   // reduce height a bit to avoid showing scrollbar too early
        ImGui::SetCursorPos(cursor_pos_backup);
    }
}

void ManifoldApp::UI_Actions()
{
    //
    // LAMBDAS: Sub widgets / sub procedures
    //
    auto _drawActionBlock = [this]() {

    };

    //
    // Main Window
    //
    if (ImGui::BeginChild("Actions"))
    {
        {
            constexpr static ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_Resizable | ImGuiTableFlags_ContextMenuInBody;

            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 2.0f));

            if (ImGui::BeginTable("Actions_Window_Main", 2, flags, ImGui::GetContentRegionAvail()))
            {
                ImGui::TableNextRow();

                //
                // LEFT PANEL: Actions list
                //
                ImGui::TableSetColumnIndex(0);

                if (ImGui::BeginChild("Actions_List", ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize))
                {
                    const NodeRegistry& reg = NodeRegistry::getInstance();
                    const auto categories   = reg.listCategories();

                    for (const auto& cat : categories)
                    {
                        if (ImGui::CollapsingHeader(cat.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
                        {
                            for (const NodeDescriptor* d : reg.listByCategory(cat))
                            {
                                ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, ImVec2(0.1f, 0.5f)); // Button text align at left, with slight indent
                                const ImVec4 bg = ImGui::GetStyle().Colors[ImGuiCol_WindowBg];
                                const ImVec4 btnOrigColor = ImGui::GetStyle().Colors[ImGuiCol_Button];    // Use original default color for list's hover color
                                ImGui::PushStyleColor(ImGuiCol_Button, bg);
                                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, btnOrigColor);

                                if (ImGui::Button(d->displayName.c_str(), ImVec2(ImGui::GetContentRegionAvail().x, 0)))
                                {
                                    try 
                                    {
                                        auto node = NodeRegistry::getInstance().create(d->id);
                                        {
                                            std::scoped_lock lock(nodeChainMutex);
                                            nodeChain.emplace_back(std::move(node));
                                        }

                                        if (nodeChain.back()->nodeHint() == "DSP")
                                        {
                                            // If it's a DSP node, fetch its parameter definitions and cache them for later use in UI
                                            // (e.g. To load default values)
                                            std::vector<AudioProcessorParam> paramDefs;
                                            dynamic_cast<DSPNode*>(nodeChain.back().get())->fetchProcessorParamList(paramDefs);
                                            const auto name = nodeChain.back()->name();
                                            dspNodeParamDefCache[name] = std::move(paramDefs);
                                        }
                                    }
                                    catch (const std::exception& e)
                                    {
                                        // TODO: Handle error by showing a message box or notification on UI. To be implemented.
                                    }
                                }
                                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay)
                                    && d->description.size() > 0)
                                    ImGui::SetTooltip("%s", d->description.c_str());

                                ImGui::PopStyleColor(2);
                                ImGui::PopStyleVar();
                            }
                        }
                    }
                }
                ImGui::EndChild();

                //
                // RIGHT PANEL: Action configuration
                //
                ImGui::TableSetColumnIndex(1);

                if (ImGui::BeginChild("Actions_Editor", ImVec2(0, 0), ImGuiWindowFlags_AlwaysAutoResize))
                {
                    // DEBUG: Just display the current node chain as text for now. Later this will be the actual configuration panel for each node.
                    ImGui::SeparatorText("Current Node Chain");

                    // ── Drag-and-drop reorder state (persists across frames) ─────────────
                    int&  s_dragSourceIdx = this->dragDropState.dragSourceIdx;   // index of item being dragged (-1 = none)
                    int&  s_dropTargetIdx = this->dragDropState.dropTargetIdx;   // insertion point (0..N)
                    bool& s_isDragging    = this->dragDropState.isDragging;     // true once mouse moved past threshold

                    // Per-frame bounding data for each item (screen Y coords)
                    const size_t chainSize = nodeChain.size();
                    std::vector<float> itemTopY(chainSize, 0.0f);
                    std::vector<float> itemBotY(chainSize, 0.0f);

                    // FontAwesome font is at Fonts[1] (loaded separately in Main.cpp)
                    ImFont* faFont = (ImGui::GetIO().Fonts->Fonts.Size > 1)
                                     ? ImGui::GetIO().Fonts->Fonts[1] : nullptr;

                    for (size_t i = 0; i < chainSize; i++)
                    {
                        {
                            const auto& currentNode = nodeChain[i].get();
                            constexpr auto actionEditorFlags = 0;//ImGuiWindowFlags_MenuBar;

                            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 5.0f);
                            ImGui::PushID(reinterpret_cast<uintptr_t>(currentNode));

                            float uiWidth, uiHeight;
                            currentNode->getUiSize(uiWidth, uiHeight);

                            // Record top Y of this item (screen coords) before drawing
                            itemTopY[i] = ImGui::GetCursorScreenPos().y;

                            // Dim the item currently being dragged so position is visually clear
                            const bool isBeingDragged = s_isDragging && (s_dragSourceIdx == (int)i);
                            if (isBeingDragged)
                                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);

                            if (ImGui::BeginChild("SingleActionEditor", ImVec2(uiWidth, uiHeight), ImGuiChildFlags_Borders, actionEditorFlags))
                            {
                                // Topbar
                                ImGui::BeginGroup();
                                {
                                    // ── Drag handle ──────────────────────────────────────
                                    {
                                        if (faFont) ImGui::PushFont(faFont, 14.0f);
                                        ImGui::TextUnformatted(ICON_FA_GRIP_VERTICAL);
                                        if (faFont) ImGui::PopFont();

                                        if (ImGui::IsItemHovered())
                                            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

                                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                                            ImGui::SetTooltip("Drag to reorder node chain");

                                        // Initiate drag when grip is pressed
                                        if (ImGui::IsItemHovered()
                                            && ImGui::IsMouseDown(ImGuiMouseButton_Left)
                                            && s_dragSourceIdx == -1)
                                            s_dragSourceIdx = (int)i;
                                    }
                                    ImGui::SameLine(0, 10);
                                    // ─────────────────────────────────────────────────────

                                    ImGui::Text("[%02llu] %s", i, currentNode->name().c_str());
                                    ImGui::SameLine();

                                    constexpr float toolButtonWidth = 25.0f;
                                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - toolButtonWidth);
                                    if (faFont) ImGui::PushFont(faFont, 14.0f);
                                    if (ImGui::Button(ICON_FA_BACKSPACE, ImVec2(toolButtonWidth, 0)))
                                    {
                                        // Reset drag state on remove
                                        dragDropState.reset();

                                        {
                                            std::scoped_lock lock(nodeChainMutex);
                                            nodeChain.erase(nodeChain.begin() + i);
                                        }

                                        // IMPORTANT:
                                        // After erasing the node, the current ImGui group for this item becomes invalid (since the underlying node object is destroyed),
                                        // so we must give end to the rest of the code in this block with a break statement to avoid messing up the ImGui state.
                                        if (faFont) ImGui::PopFont();
                                        ImGui::EndGroup();
                                        ImGui::EndChild();
                                        if (isBeingDragged) ImGui::PopStyleVar(); // pop Alpha
                                        ImGui::PopID();
                                        ImGui::PopStyleVar();                      // pop ChildRounding
                                        break;  // IMPORTANT: break here to avoid accessing invalid memory after erase
                                    }
                                    if (faFont) ImGui::PopFont();
                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                                        ImGui::SetTooltip("Remove current node");

                                    ImGui::Separator();
                                    ImGui::EndGroup();
                                }

                                // Node-specific UI
                                currentNode->drawUI();       
                            }
                            ImGui::EndChild(); 

                            if (isBeingDragged)
                                ImGui::PopStyleVar(); // pop Alpha

                            // Record bottom Y after child ends
                            itemBotY[i] = ImGui::GetCursorScreenPos().y;

                            ImGui::PopID();
                            ImGui::PopStyleVar();

                            // Add a neat margin
                            ImGui::Dummy(ImVec2(0, 8));
                        }
                    }

                    // ── Drag-and-drop logic (runs every frame after the list loop) ────────
                    _dragDropIdle(itemTopY, itemBotY);

                    //
                    // FOOTER: Output settings
                    //
                    ImGuiExt::MakeFooter(200.0f);
                    ImGui::Separator();
                    
                    {
                        ImGui::BeginGroup();

                        ImGui::Text("Folder");
                        ImGui::SameLine(0, 30);

                        const char* folderButtonLabel = outputPath.empty() ? "(empty)" : outputPath.c_str();
                        if (ImGui::Button(folderButtonLabel, ImVec2(ImGui::GetContentRegionAvail().x - 5.0f, 0)))
                        {
                            nfdu8char_t* pickedPath = nullptr;
                            if (NFD::PickFolder(pickedPath) == NFD_OKAY)
                            {
                                outputPath = pickedPath;
                                NFD::FreePath(pickedPath);
                            }
                        }
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                        {
                            ImGui::BeginTooltip();
                            ImGui::Text("Click this button to specify output path.");
                            ImGui::Separator();
                            ImGui::BulletText("Current output path: %s", outputPath.empty() ? "Not specified" : outputPath.c_str());
                            ImGui::EndTooltip();
                        }

                        ImGui::EndGroup();    
                    }

                    {
                        ImGui::BeginGroup();

                        ImGui::Text("File Name");

                        ImGui::EndGroup();
                    }

                    // Output format selector
                    {
                        ImGui::BeginGroup();

                        constexpr float kLabelW = 60.0f;

                        // ── Container format ──────────────────────────────
                        {
                            struct FmtEntry { ContainerFormat fmt; const char* label; };
                            static constexpr FmtEntry kFormats[] = {
                                { ContainerFormat::Wav,  "WAV (.wav)"        },
                                { ContainerFormat::Flac, "FLAC (.flac)"      },
                                { ContainerFormat::Ogg,  "OGG Vorbis (.ogg)" },
                                { ContainerFormat::Opus, "Opus (.ogg)"       },
                                { ContainerFormat::Aiff, "AIFF (.aiff)"      },
                                { ContainerFormat::Caf,  "CAF (.caf)"        },
                                { ContainerFormat::W64,  "Wave64 (.w64)"     },
                            };

                            const char* fmtPreview = "?";
                            for (const auto& e : kFormats)
                                if (e.fmt == outputFormat) { fmtPreview = e.label; break; }

                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Format");
                            ImGui::SameLine(kLabelW);
                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 5.0f);
                            if (ImGui::BeginCombo("##out_fmt", fmtPreview))
                            {
                                for (const auto& e : kFormats)
                                {
                                    const bool sel = (e.fmt == outputFormat);
                                    if (ImGui::Selectable(e.label, sel))
                                        outputFormat = e.fmt;
                                    if (sel) ImGui::SetItemDefaultFocus();
                                }
                                ImGui::EndCombo();
                            }
                        }

                        ImGui::Spacing();

                        // ── Sample subtype ────────────────────────────────
                        {
                            const bool codecLocked = (outputFormat == ContainerFormat::Ogg ||
                                                      outputFormat == ContainerFormat::Opus);
                            const bool isFlac      = (outputFormat == ContainerFormat::Flac);

                            struct SubEntry { SubtypeOverride sub; const char* label; bool flacOk; };
                            static constexpr SubEntry kSubtypes[] = {
                                { SubtypeOverride::Auto,     "Auto (format default)", true  },
                                { SubtypeOverride::Pcm16,    "PCM 16-bit",            true  },
                                { SubtypeOverride::Pcm24,    "PCM 24-bit",            true  },
                                { SubtypeOverride::Pcm32,    "PCM 32-bit",            false },
                                { SubtypeOverride::Float32,  "Float 32-bit",          false },
                                { SubtypeOverride::Double64, "Double 64-bit",         false },
                            };

                            // Compute preview label, accounting for clamping
                            const char* subPreview;
                            if (codecLocked)
                            {
                                subPreview = (outputFormat == ContainerFormat::Opus)
                                             ? "Opus (fixed)" : "Vorbis (fixed)";
                            }
                            else if (isFlac
                                     && outputSubtype != SubtypeOverride::Auto
                                     && outputSubtype != SubtypeOverride::Pcm16)
                            {
                                subPreview = "PCM 24-bit (clamped)";
                            }
                            else
                            {
                                subPreview = "?";
                                for (const auto& e : kSubtypes)
                                    if (e.sub == outputSubtype) { subPreview = e.label; break; }
                            }

                            ImGui::AlignTextToFramePadding();
                            ImGui::Text("Subtype");
                            ImGui::SameLine(kLabelW);
                            ImGui::BeginDisabled(codecLocked);
                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 5.0f);
                            if (ImGui::BeginCombo("##out_sub", subPreview))
                            {
                                for (const auto& e : kSubtypes)
                                {
                                    const bool unsupported = isFlac && !e.flacOk;
                                    if (unsupported) ImGui::BeginDisabled(true);
                                    const bool sel = (e.sub == outputSubtype);
                                    if (ImGui::Selectable(e.label, sel) && !unsupported)
                                        outputSubtype = e.sub;
                                    if (sel && !unsupported) ImGui::SetItemDefaultFocus();
                                    if (unsupported) ImGui::EndDisabled();
                                }
                                ImGui::EndCombo();
                            }
                            ImGui::EndDisabled();
                        }

                        ImGui::EndGroup();
                    }

                    {
                        ImGui::Spacing();

                        // ── Process button ───────────────────────────────────
                        const bool canProcess = !sndFileList.empty()
                                             && !nodeChain.empty()
                                             && !outputPath.empty();
                        ImGui::BeginDisabled(!canProcess);
                        if (ImGui::Button("Process All Files", ImVec2(-1, 28.0f)))
                        {
                            // Snapshot node names for the run record
                            std::vector<std::string> nodeNames;
                            {
                                std::scoped_lock lock(nodeChainMutex);
                                for (const auto& n : nodeChain)
                                    nodeNames.push_back(n->name());
                            }

                            auto run = std::make_shared<ProcessingRun>(
                                nextRunId++, outputPath, std::move(nodeNames));

                            singleFileProcessorWorker.setOutputDir(outputPath);
                            singleFileProcessorWorker.setOutputFormat(outputFormat, outputSubtype);

                            {
                                std::scoped_lock lock(sndFileListMutex);
                                for (auto& fi : sndFileList)
                                {
                                    auto record = std::make_shared<FileRunRecord>(fi);
                                    run->records.push_back(record);
                                    singleFileProcessorWorker.addFile(fi, std::move(record));
                                }
                            }

                            processingRuns.push_back(std::move(run));
                        }
                        ImGui::EndDisabled();

                        if (!canProcess)
                        {
                            ImGui::BeginGroup();

                            const char* hintMsg = nullptr;
                            if (outputPath.empty())
                                hintMsg = "Select an output folder above to enable processing.";
                            else if (sndFileList.empty())
                                hintMsg = "Go to Files, add files to the file list to enable processing.";
                            else
                                hintMsg = "Add at least one action node to enable processing.";

                            // Calculate text widths & gap widths for centralized display
                            constexpr float gap = 4.0f;
                            const float hintMsgWidth = ImGui::CalcTextSize(ICON_FA_EXCLAMATION_TRIANGLE).x + gap + ImGui::CalcTextSize(hintMsg).x;
                            ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x * 0.5f - hintMsgWidth * 0.5f);

                            ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", ICON_FA_EXCLAMATION_TRIANGLE);
                            ImGui::SameLine(0, 4);
                            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f), "%s", hintMsg);

                            ImGui::EndGroup();
                        }

                        // ── Latest run status ────────────────────────────────
                        if (!processingRuns.empty())
                        {
                            ImGui::Spacing();
                            ImGui::SeparatorText("Latest Run");

                            const auto& run   = processingRuns.back();
                            const size_t done  = run->countByStatus(FileRunRecord::Status::Done);
                            const size_t error = run->countByStatus(FileRunRecord::Status::Error);
                            const size_t total = run->records.size();

                            ImGui::Text("Run #%llu  —  %zu / %zu done, %zu error(s)",
                                        run->id, done + error, total, error);
                            ImGui::ProgressBar(run->getProgress(), ImVec2(-1, 6), "");  // Param #3: overlay. Set to empty string to disable percentage display

                            // Per-file rows
                            ImGui::BeginChild("RunFileList", ImVec2(0, 120),
                                              ImGuiChildFlags_Borders);
                            for (const auto& rec : run->records)
                            {
                                const auto st = rec->status.load(std::memory_order_relaxed);

                                const char* statusLabel;
                                ImVec4      statusColor;
                                switch (st)
                                {
                                    case FileRunRecord::Status::Processing:
                                        statusLabel = " RUN ";
                                        statusColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
                                        break;
                                    case FileRunRecord::Status::Done:
                                        statusLabel = "  OK ";
                                        statusColor = ImVec4(0.4f, 1.0f, 0.4f, 1.0f);
                                        break;
                                    case FileRunRecord::Status::Error:
                                        statusLabel = " ERR ";
                                        statusColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
                                        break;
                                    default: // Pending
                                        statusLabel = " ... ";
                                        statusColor = ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
                                        break;
                                }

                                ImGui::TextColored(statusColor, "%s", statusLabel);
                                ImGui::SameLine();
                                ImGui::TextUnformatted(rec->fileInfo->fileNameBase.c_str());

                                if (st == FileRunRecord::Status::Processing)
                                {
                                    std::scoped_lock<std::mutex> rlock(rec->progressMutex);
                                    if (!rec->currentNodeName.empty())
                                    {
                                        ImGui::SameLine();
                                        ImGui::TextDisabled("[%s]", rec->currentNodeName.c_str());
                                    }
                                }
                                else if (st == FileRunRecord::Status::Error)
                                {
                                    if (ImGui::IsItemHovered())
                                    {
                                        std::scoped_lock<std::mutex> rlock(rec->progressMutex);
                                        if (!rec->errorMessage.empty())
                                            ImGui::SetTooltip("%s", rec->errorMessage.c_str());
                                    }
                                }
                            }
                            ImGui::EndChild();
                        }
                    }
                }
                ImGui::EndChild();

                // HACK: Set minimum column width
                // Make sure reference text (for example, the warning/tip text for "Process All Files" button) can be fully shown.
                constexpr const char* referenceText = ICON_FA_EXCLAMATION_TRIANGLE " Go to Files, add files to the file list to enable processing.";
                ImGuiHack::SetTableMinColumnWidth(ImGui::CalcTextSize(referenceText).x + ImGui::GetStyle().CellPadding.x * 2.0f);

                ImGui::EndTable();
            }

            ImGui::PopStyleVar();
        }
    }
    ImGui::EndChild();
}

void ManifoldApp::_dragDropIdle(const std::vector<float>& itemTopY, const std::vector<float>& itemBotY)
{
    int&  s_dragSourceIdx = dragDropState.dragSourceIdx;
    int&  s_dropTargetIdx = dragDropState.dropTargetIdx;
    bool& s_isDragging    = dragDropState.isDragging;

    if (s_dragSourceIdx < 0 || s_dragSourceIdx >= (int)nodeChain.size())
        return;

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        // Mouse released: perform reorder if drop position is meaningful
        if (s_isDragging
            && s_dropTargetIdx >= 0
            && s_dropTargetIdx != s_dragSourceIdx
            && s_dropTargetIdx != s_dragSourceIdx + 1)
        {
            std::scoped_lock lock(nodeChainMutex);
            auto node = std::move(nodeChain[s_dragSourceIdx]);
            nodeChain.erase(nodeChain.begin() + s_dragSourceIdx);
            int insertAt = (s_dropTargetIdx > s_dragSourceIdx)
                           ? s_dropTargetIdx - 1
                           : s_dropTargetIdx;
            nodeChain.insert(nodeChain.begin() + insertAt, std::move(node));
        }
        
        dragDropState.reset();
        return;
    }

    // Activate drag once mouse moves beyond threshold
    if (!s_isDragging)
    {
        ImVec2 delta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 3.0f);
        if (delta.x != 0.0f || delta.y != 0.0f)
            s_isDragging = true;
    }

    if (!s_isDragging || (int)itemTopY.size() != (int)nodeChain.size())
        return;

    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);

    // Compute drop target: compare mouse Y against each item's midpoint
    const float mouseY = ImGui::GetMousePos().y;
    s_dropTargetIdx = (int)nodeChain.size(); // default: after last
    for (int j = 0; j < (int)nodeChain.size(); j++)
    {
        const float midY = (itemTopY[j] + itemBotY[j]) * 0.5f;
        if (mouseY < midY)
        {
            s_dropTargetIdx = j;
            break;
        }
    }

    // Determine indicator line Y (between items / at edges)
    float indicatorY = 0.0f;
    if (!itemTopY.empty())
    {
        if (s_dropTargetIdx <= 0)
            indicatorY = itemTopY[0];
        else if (s_dropTargetIdx >= (int)nodeChain.size())
            indicatorY = itemBotY.back();
        else
            indicatorY = itemTopY[s_dropTargetIdx];
    }

    // Draw Windows-Explorer-style drop indicator:
    // a horizontal line with filled circle caps
    const ImU32 lineColor = IM_COL32(30, 144, 255, 230);
    ImDrawList* drawList  = ImGui::GetWindowDrawList();
    const float x0        = ImGui::GetWindowPos().x + 8.0f;
    const float x1        = ImGui::GetWindowPos().x + ImGui::GetWindowWidth() - 8.0f;

    drawList->AddLine(
        ImVec2(x0 + 8.0f, indicatorY),
        ImVec2(x1,         indicatorY),
        lineColor, 2.0f);
    drawList->AddCircleFilled(ImVec2(x0 + 4.0f, indicatorY), 4.0f, lineColor);
    drawList->AddCircleFilled(ImVec2(x1,         indicatorY), 4.0f, lineColor);
}
