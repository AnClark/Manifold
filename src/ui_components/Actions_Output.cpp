#include "./Actions.hpp"
#include "Main.hpp"
#include "base/AudioFormats.hpp"
#include "../fonts/IconFontAwesome5_Unique.h"

#include <imgui.h>
#include "utils/NFDIncludes.h"  // IWYU pragma: keep
#include "ImGuiNotify_MOD.hpp"

#include <algorithm>
#include <filesystem>
#include <mutex>

void UIComponents_Actions::command_StartProcessingAllFiles()
{
    // Snapshot node names for the run record
    std::vector<std::string> nodeNames;
    {
        std::scoped_lock lock(app->nodeChainMutex);
        for (const auto& n : app->nodeChain)
            nodeNames.push_back(n->name());
    }

    // Create a new processing run record and kick off the processing worker with the current file list and node chain.
    auto run = std::make_shared<ProcessingRun>(
        app->nextRunId++, app->outputPath, std::move(nodeNames));

    // Configure the single-file processor worker with the current output settings.
    // These settings will be used when processing each file.
    app->singleFileProcessorWorker.setOutputDir(app->outputPath);
    app->singleFileProcessorWorker.setOutputFormat(app->outputFormat, app->outputSubtype);
    app->singleFileProcessorWorker.setSinkMode(app->nullOutput ? SinkMode::Null : SinkMode::WriteFile);
    app->singleFileProcessorWorker.setConflictPolicy(app->outputFilenameTemplate.conflictPolicy);

    // Now that the run record and worker are configured, we can safely add files to the worker's queue.
    {
        std::scoped_lock lock(app->sndFileListMutex);
        int fileCounter = 0;
        for (auto& fi : app->sndFileList)
        {
            // Accumulate file counter for filename template processing.
            ++fileCounter;

            // Resolve the source file stem for filename template processing. This is just the filename without directory or extension.
            // To deal with CJK filenames, we use the UTF-8 API of std::filesystem and treat the stem as a UTF-8 string.
            const std::string sourceStem =
                std::filesystem::path(fi->filePath).stem().u8string();

            // Create a file run record for this file and add it to the current run.
            // The file run record will track the processing status and results for this file across the node chain.
            auto record = std::make_shared<FileRunRecord>(fi);

            // Based on the output filename template, resolve (render) the source file stem and file counter into the final output file stem
            record->resolvedOutputStem =
                app->outputFilenameTemplate.resolve(sourceStem, fileCounter);

            // Wire the run's cancel token so the worker can interrupt processing mid-file.
            record->runCancelToken = &run->cancelRequested;

            // Now that we have the resolved output stem, we can determine the final output path for this file and store it in the record.
            // Then, we can send the file to the single-file processor worker.
            run->records.push_back(record);
            app->singleFileProcessorWorker.addFile(fi, std::move(record));
        }
    }

    // Finally, add the run record to the app's history.
    // The processing worker will update the file run records in-place as it processes each file,
    // and the UI will reflect the status updates in real time.
    app->processingRuns.push_back(std::move(run));
}

void UIComponents_Actions::button_SelectOutputFolder()
{
    const char* folderButtonLabel = app->outputPath.empty() ? "(empty)" : app->outputPath.c_str();
    if (ImGui::Button(folderButtonLabel, ImVec2(ImGui::GetContentRegionAvail().x - 5.0f, 0)))
    {
        nfdu8char_t* pickedPath = nullptr;

        // Pass in the parent window handle: On Windows, the parent window will be automatically disabled while the file dialog is open,
        // preventing users from accidentally interacting with the main interface while the file dialog is open.
        nfdwindowhandle_t parentWindow = {};
        NFD_GetNativeWindowFromGLFWWindow(app->getWindow(), &parentWindow);

        if (NFD::PickFolder(pickedPath, nullptr, parentWindow) == NFD_OKAY)
        {
            app->outputPath = pickedPath;
            NFD::FreePath(pickedPath);
        }
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
    {
        ImGui::BeginTooltip();
        ImGui::Text("Click this button to specify output path.");
        ImGui::Separator();
        ImGui::BulletText("Current output path: %s", app->outputPath.empty() ? "Not specified" : app->outputPath.c_str());
        ImGui::EndTooltip();
    }
}

void UIComponents_Actions::popup_ConfirmSetOutputFolder()
{
    const auto closePopup = [this]() {
        pendingDialog = UIComponents_Actions::PendingDialog::Null;
        ImGui::CloseCurrentPopup();
    };

    if (ImGui::BeginPopupModal("##set_output_folder_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Set the path you dragged here as output folder?");
        ImGui::BulletText("%s", dndReceivedPath.c_str());
        ImGui::Separator();

        if (ImGui::Button("Confirm", ImVec2(120, 0)))
        {
            app->outputPath = dndReceivedPath;
            closePopup();
        }
        ImGui::SameLine();

        if (ImGui::Shortcut(ImGuiKey_Escape))
            closePopup();

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            closePopup();

        ImGui::EndPopup();
    }
}

void UIComponents_Actions::button_FileName()
{
    // State for the filename config popup (persists while the modal is open).
    FilenameTemplate& s_editTemplate     = app->fileNameEditorState.editTemplate;
    int&              s_selectedTokenIdx = app->fileNameEditorState.selectedTokenIdx;

    // Build a compact summary of the current template for the button label.
    std::string fnSummary;
    {
        const auto& segs = app->outputFilenameTemplate.segments;
        if (segs.empty())
        {
            fnSummary = "(not configured)";
        }
        else
        {
            for (size_t si = 0; si < segs.size(); ++si)
            {
                const auto& tok = segs[si].token;
                switch (tok.type)
                {
                    case FilenameToken::Type::OriginalName: fnSummary += "Name";    break;
                    case FilenameToken::Type::LiteralText:  fnSummary += "\"" + tok.literalText + "\""; break;
                    case FilenameToken::Type::Counter:
                    {
                        char buf[32];
                        snprintf(buf, sizeof(buf), "Counter(%0*d)",
                                    tok.counterPad, tok.counterStart);
                        fnSummary += buf;
                        break;
                    }
                }
                if (!segs[si].separator.empty())
                    fnSummary += " + \"" + segs[si].separator + "\" + ";
                else if (si + 1 < segs.size())
                    fnSummary += " + ";
            }
        }
    }

    if (ImGui::Button(fnSummary.c_str(),
                        ImVec2(ImGui::GetContentRegionAvail().x - 5.0f, 0)))
    {
        s_editTemplate     = app->outputFilenameTemplate;
        s_selectedTokenIdx = -1;
        ImGui::OpenPopup("Output File Name Rule##FilenameConfigModal");
    }
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
        ImGui::SetTooltip("Click to configure output file naming rules");

    // Build the Output File Name config dialog
    popup_OutputFileNameRule(s_editTemplate, s_selectedTokenIdx);
}

void UIComponents_Actions::popup_OutputFileNameRule(FilenameTemplate& s_editTemplate, int& s_selectedTokenIdx)
{
    ImGui::SetNextWindowSize(ImVec2(620, 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopupModal("Output File Name Rule##FilenameConfigModal", nullptr,
                                ImGuiWindowFlags_AlwaysAutoResize))
    {
        // ── Token palette (add tokens) ────────────────────────────
        ImGui::SeparatorText("Add Token");
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("Click to add token:");
        ImGui::SameLine();
        if (ImGui::Button("Original Name"))
        {
            s_editTemplate.segments.push_back(
                { FilenameToken{ FilenameToken::Type::OriginalName }, "" });
            s_selectedTokenIdx = static_cast<int>(s_editTemplate.segments.size()) - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Counter"))
        {
            FilenameToken ct;
            ct.type = FilenameToken::Type::Counter;
            s_editTemplate.segments.push_back({ ct, "" });
            s_selectedTokenIdx = static_cast<int>(s_editTemplate.segments.size()) - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Custom Text"))
        {
            FilenameToken lt;
            lt.type = FilenameToken::Type::LiteralText;
            lt.literalText = "text";
            s_editTemplate.segments.push_back({ lt, "" });
            s_selectedTokenIdx = static_cast<int>(s_editTemplate.segments.size()) - 1;
        }

        ImGui::Spacing();
        ImGui::Spacing();

        // ── Template strip (draggable token chips) ────────────────
        ImGui::SeparatorText("Template (drag to reorder, click to edit)");
        ImGui::Spacing();

        const ImVec4 kColorOrigName   = ImVec4(0.20f, 0.50f, 0.85f, 1.0f);
        const ImVec4 kColorCounter    = ImVec4(0.20f, 0.65f, 0.35f, 1.0f);
        const ImVec4 kColorLiteral    = ImVec4(0.65f, 0.40f, 0.10f, 1.0f);
        const ImVec4 kColorSelected   = ImVec4(1.0f,  0.80f, 0.20f, 1.0f);

        bool stripModified = false;
        int  eraseIdx      = -1;
        int  dndSrc        = -1;
        int  dndDst        = -1;

        ImGui::BeginGroup();
        for (int ti = 0; ti < (int)s_editTemplate.segments.size(); ++ti)
        {
            auto& seg = s_editTemplate.segments[ti];
            ImGui::PushID(ti);

            // Chip label
            char chipLabel[64];
            switch (seg.token.type)
            {
                case FilenameToken::Type::OriginalName:
                    snprintf(chipLabel, sizeof(chipLabel), "Name");
                    break;
                case FilenameToken::Type::Counter:
                    snprintf(chipLabel, sizeof(chipLabel),
                                "Counter(%0*d)", seg.token.counterPad, seg.token.counterStart);
                    break;
                case FilenameToken::Type::LiteralText:
                    snprintf(chipLabel, sizeof(chipLabel),
                                "\"%s\"", seg.token.literalText.c_str());
                    break;
            }

            const bool selected = (s_selectedTokenIdx == ti);
            const ImVec4& chipColor = selected ? kColorSelected :
                (seg.token.type == FilenameToken::Type::OriginalName ? kColorOrigName :
                    seg.token.type == FilenameToken::Type::Counter      ? kColorCounter  :
                                                                        kColorLiteral);

            ImGui::PushStyleColor(ImGuiCol_Button,        chipColor);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                ImVec4(chipColor.x * 1.2f, chipColor.y * 1.2f,
                        chipColor.z * 1.2f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,  chipColor);

            if (ImGui::Button(chipLabel))
                s_selectedTokenIdx = ti;

            // Drag-and-drop source
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
            {
                ImGui::SetDragDropPayload("FN_TOKEN_IDX", &ti, sizeof(int));
                ImGui::TextUnformatted(chipLabel);
                ImGui::EndDragDropSource();
            }

            // Drag-and-drop target (drop BEFORE this chip)
            if (ImGui::BeginDragDropTarget())
            {
                if (const ImGuiPayload* p =
                        ImGui::AcceptDragDropPayload("FN_TOKEN_IDX"))
                {
                    dndSrc = *(const int*)p->Data;
                    dndDst = ti;
                }
                ImGui::EndDragDropTarget();
            }

            ImGui::PopStyleColor(3);

            // [×] Remove button
            ImGui::SameLine(0, 2);
            ImGui::PushStyleColor(ImGuiCol_Button,
                ImVec4(0.5f, 0.12f, 0.12f, 1.0f));
            if (ImGui::SmallButton("x"))
                eraseIdx = ti;
            ImGui::PopStyleColor();

            // Separator text box (after this token)
            ImGui::SameLine(0, 8);
            ImGui::TextDisabled("+");
            ImGui::SameLine(0, 4);
            char sepBuf[32];
            snprintf(sepBuf, sizeof(sepBuf), "%s", seg.separator.c_str());
            ImGui::SetNextItemWidth(44.0f);
            if (ImGui::InputText("##sep", sepBuf, sizeof(sepBuf)))
                seg.separator = sepBuf;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal))
                ImGui::SetTooltip("Separator appended after this token");
            ImGui::SameLine(0, 8);

            ImGui::PopID();
        }

        // Drop target for "append at end"
        ImGui::InvisibleButton("##dnd_end", ImVec2(20, ImGui::GetFrameHeight()));
        if (ImGui::BeginDragDropTarget())
        {
            if (const ImGuiPayload* p =
                    ImGui::AcceptDragDropPayload("FN_TOKEN_IDX"))
            {
                dndSrc = *(const int*)p->Data;
                dndDst = static_cast<int>(s_editTemplate.segments.size());
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::EndGroup();

        // Apply DnD reorder
        if (dndSrc >= 0 && dndDst >= 0 && dndSrc != dndDst)
        {
            auto segCopy = s_editTemplate.segments[dndSrc];
            s_editTemplate.segments.erase(
                s_editTemplate.segments.begin() + dndSrc);
            int insertAt = dndDst;
            insertAt = std::clamp(insertAt, 0,
                                    (int)s_editTemplate.segments.size());
            s_editTemplate.segments.insert(
                s_editTemplate.segments.begin() + insertAt, segCopy);
            // Update selected index for ALL chips, not just the dragged one.
            // After erase at dndSrc + insert at insertAt, any chip whose
            // position changed must have its index adjusted.
            if (s_selectedTokenIdx == dndSrc)
            {
                s_selectedTokenIdx = insertAt;
            }
            else if (s_selectedTokenIdx >= 0)
            {
                int adj = s_selectedTokenIdx;
                if (adj > dndSrc)   adj--;  // shifted left by erase
                if (adj >= insertAt) adj++; // shifted right by insert
                s_selectedTokenIdx = adj;
            }
            stripModified = true;
        }
        // Apply erase
        if (eraseIdx >= 0)
        {
            s_editTemplate.segments.erase(
                s_editTemplate.segments.begin() + eraseIdx);
            if (s_selectedTokenIdx == eraseIdx)
                // The selected chip was deleted; clamp to new last index
                s_selectedTokenIdx = std::min(s_selectedTokenIdx,
                                                (int)s_editTemplate.segments.size() - 1);
            else if (s_selectedTokenIdx > eraseIdx)
                // Selected chip shifted left
                s_selectedTokenIdx--;
            stripModified = true;
        }
        (void)stripModified;

        // ── Selected token parameter editor ───────────────────────
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (s_selectedTokenIdx >= 0 &&
            s_selectedTokenIdx < (int)s_editTemplate.segments.size())
        {
            auto& selSeg = s_editTemplate.segments[s_selectedTokenIdx];
            ImGui::TextDisabled("Token settings (No. %2d)", s_selectedTokenIdx);

            switch (selSeg.token.type)
            {
                case FilenameToken::Type::OriginalName:
                    ImGui::TextDisabled("  (Original Name — no parameters)");
                    break;

                case FilenameToken::Type::LiteralText:
                {
                    char buf[256];
                    snprintf(buf, sizeof(buf), "%s",
                                selSeg.token.literalText.c_str());
                    ImGui::SetNextItemWidth(200.0f);
                    if (ImGui::InputText("Text##littext", buf, sizeof(buf)))
                        selSeg.token.literalText = buf;
                    break;
                }

                case FilenameToken::Type::Counter:
                {
                    ImGui::SetNextItemWidth(70.0f);
                    ImGui::InputInt("Start##ctr_start",
                                    &selSeg.token.counterStart);
                    selSeg.token.counterStart =
                        std::max(0, selSeg.token.counterStart);
                    ImGui::SameLine(0, 25.0f);
                    ImGui::SetNextItemWidth(70.0f);
                    ImGui::InputInt("Step##ctr_step",
                                    &selSeg.token.counterStep);
                    selSeg.token.counterStep =
                        std::max(1, selSeg.token.counterStep);
                    ImGui::SameLine(0, 25.0f);
                    ImGui::SetNextItemWidth(70.0f);
                    ImGui::InputInt("Padding##ctr_pad",
                                    &selSeg.token.counterPad);
                    selSeg.token.counterPad =
                        std::clamp(selSeg.token.counterPad, 1, 8);
                    break;
                }
            }
        }
        else
        {
            ImGui::TextDisabled("  (no token selected)");
        }

        // ── Options ───────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::SeparatorText("Options");

        {
            constexpr const char* kPolicies[] =
                { "Auto Rename", "Overwrite", "Skip" };
            constexpr FilenameTemplate::ConflictPolicy kPolicyValues[] = {
                FilenameTemplate::ConflictPolicy::AutoRename,
                FilenameTemplate::ConflictPolicy::Overwrite,
                FilenameTemplate::ConflictPolicy::Skip,
            };
            int policyIdx = 0;
            for (int pi = 0; pi < 3; ++pi)
                if (s_editTemplate.conflictPolicy == kPolicyValues[pi])
                    { policyIdx = pi; break; }
            ImGui::SetNextItemWidth(140.0f);
            if (ImGui::Combo("If file exists##conflict",
                                &policyIdx, kPolicies, 3))
                s_editTemplate.conflictPolicy = kPolicyValues[policyIdx];
        }
        ImGui::SameLine(0, 20);
        ImGui::Checkbox("Replace spaces with '_'",
                        &s_editTemplate.sanitizeSpaces);

        // ── Preview ───────────────────────────────────────────────
        ImGui::Spacing();
        ImGui::Spacing();

        // Validate before preview
        const auto validationErr = s_editTemplate.validate();
        if (validationErr.has_value())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f),
                                "%s  %s",
                                ICON_FA_EXCLAMATION_TRIANGLE,
                                validationErr->c_str());
        }
        else
        {
            ImGui::SeparatorText("Preview");
            // Build preview using the first few files from sndFileList,
            // or placeholder names if the list is empty.
            const std::string kPlaceholders[] = {
                "explosion_large_raw",
                "footstep_dirt_walk",
                "ambient_cave_loop",
            };
            const int previewCount = 3;
            for (int pi = 0; pi < previewCount; ++pi)
            {
                std::string sourceStem;
                {
                    std::scoped_lock lock(app->sndFileListMutex);
                    if (pi < (int)app->sndFileList.size())
                    {
                        sourceStem =
                            std::filesystem::u8path(app->sndFileList[pi]->filePath)
                                .stem().u8string();
                    }
                }
                if (sourceStem.empty())
                    sourceStem = kPlaceholders[pi % 3];

                const std::string ext =
                    AudioFormats::extension(app->outputFormat);
                const std::string resolved =
                    s_editTemplate.resolve(sourceStem, pi + 1)
                    + "." + ext;
                ImGui::BulletText("%s", resolved.c_str());
            }
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // ── Dialog buttons ────────────────────────────────────────
        const float btnW = 100.0f;
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - btnW * 2 - 8);

        if (ImGui::Button("Cancel", ImVec2(btnW, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::SameLine(0, 8);

        ImGui::BeginDisabled(validationErr.has_value());
        if (ImGui::Button("Apply", ImVec2(btnW, 0)))
        {
            app->outputFilenameTemplate = s_editTemplate;
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();

        ImGui::EndPopup();
    }

}

void UIComponents_Actions::combo_SelectContainerFormat()
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
        if (e.fmt == app->outputFormat) { fmtPreview = e.label; break; }

    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 5.0f);
    if (ImGui::BeginCombo("##out_fmt", fmtPreview))
    {
        for (const auto& e : kFormats)
        {
            const bool sel = (e.fmt == app->outputFormat);
            if (ImGui::Selectable(e.label, sel))
                app->outputFormat = e.fmt;
            if (sel) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
}

void UIComponents_Actions::combo_SelectSampleSubtype()
{
    const bool codecLocked = (app->outputFormat == ContainerFormat::Ogg ||
                                app->outputFormat == ContainerFormat::Opus);
    const bool isFlac      = (app->outputFormat == ContainerFormat::Flac);

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
        subPreview = (app->outputFormat == ContainerFormat::Opus)
                        ? "Opus (fixed)" : "Vorbis (fixed)";
    }
    else if (isFlac
                && app->outputSubtype != SubtypeOverride::Auto
                && app->outputSubtype != SubtypeOverride::Pcm16)
    {
        subPreview = "PCM 24-bit (clamped)";
    }
    else
    {
        subPreview = "?";
        for (const auto& e : kSubtypes)
            if (e.sub == app->outputSubtype) { subPreview = e.label; break; }
    }

    ImGui::BeginDisabled(codecLocked);
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 5.0f);
    if (ImGui::BeginCombo("##out_sub", subPreview))
    {
        for (const auto& e : kSubtypes)
        {
            const bool unsupported = isFlac && !e.flacOk;
            if (unsupported) ImGui::BeginDisabled(true);
            const bool sel = (e.sub == app->outputSubtype);
            if (ImGui::Selectable(e.label, sel) && !unsupported)
                app->outputSubtype = e.sub;
            if (sel && !unsupported) ImGui::SetItemDefaultFocus();
            if (unsupported) ImGui::EndDisabled();
        }
        ImGui::EndCombo();
    }
    ImGui::EndDisabled();
}

bool UIComponents_Actions::query_CanProcess() const
{
    return !app->sndFileList.empty()
            && !app->nodeChain.empty()
            && (app->nullOutput || !app->outputPath.empty());
}

const char* UIComponents_Actions::query_ProcessingHints() const
{
    if (app->outputPath.empty())
        return "Select an output folder above to enable processing.";
    else if (app->sndFileList.empty())
        return "Go to Files, add files to the file list to enable processing.";
    else
        return "Add at least one action node to enable processing.";    
}

bool UIComponents_Actions::query_AllFilesFullyAnalyzed() const
{
    std::scoped_lock<std::mutex> sndFileListLock(app->sndFileListMutex);

    uint32_t fileCountOK = 0;
    uint32_t fileCountErr = 0;
    for (const auto& file : app->sndFileList)
    {
        if (file->isParseOK && file->isR128ParsedOK && file->isDcOffsetCalculatedOK)
            fileCountOK++;
        if (!file->errorMsg.empty() || !file->errorMsgR128.empty() || !file->errorMsgDcOffset.empty())
            fileCountErr++;
    }

    // If analyzed file count (OK + Err) is less than the number of elements in sndFileList,
    // then we know there are still some files are being analyzed.
    return (fileCountOK + fileCountErr == app->sndFileList.size());
}

void UIComponents_Actions::info_ShowProcessingHints()
{
    if (!query_CanProcess())
    {
        ImGui::BeginGroup();

        const char* hintMsg = query_ProcessingHints();

        // Calculate text widths & gap widths for centralized display
        constexpr float gap = 4.0f;
        const float hintMsgWidth = ImGui::CalcTextSize(ICON_FA_EXCLAMATION_TRIANGLE).x + gap + ImGui::CalcTextSize(hintMsg).x;
        const float fixupOffset = ImGui::GetStyle().WindowPadding.x;    // Add a fix-up compensation to left margin, to make sure the text is centralized
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x * 0.5f - hintMsgWidth * 0.5f + fixupOffset);

        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "%s", ICON_FA_EXCLAMATION_TRIANGLE);
        ImGui::SameLine(0, 4);
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.5f, 1.0f), "%s", hintMsg);

        ImGui::EndGroup();
    }
}

void UIComponents_Actions::subUI_ShowLatestRunStatus()
{
    if (!app->processingRuns.empty())
    {
        ImGui::Spacing();
        ImGui::SeparatorText("Latest Run");

        const auto& run   = app->processingRuns.back();
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

void UIComponents_Actions::toggle_EnableNullOutput()
{
    ImGui::Checkbox("Null Output", &app->nullOutput);
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
    {
        if (ImGui::BeginTooltip())
        {
            ImGui::Text("Process audio without writing output files.");
            ImGui::Separator();
            ImGui::Text("If you only analyze audio files (e.g. via Loudness Compliance), but not need to produce any artifacts,\n"
            "this mode would be useful.");
            ImGui::EndTooltip();
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("(Useful for analysis-only pipelines)");
}

void UIComponents_Actions::button_ProcessAllFiles()
{
    // ── Process button ───────────────────────────────────
    const bool canProcess = query_CanProcess();
    ImGui::BeginDisabled(!canProcess);
    if (ImGui::Button("Process All Files", ImVec2(-1, 28.0f)))
    {
        // Check if all files have been analyzed. If not, warn user and abort.
        if (query_AllFilesFullyAnalyzed())
            command_StartProcessingAllFiles();
        else
        {
            ImGuiToast toast = {ImGuiToastType::Warning,
                                    5000, 
                                        "Please wait until all input files have been analyzed.\n"
                                               "Go to Files view for more details."};
            toast.setTitle("Wait a minute!");
            ImGui::InsertNotification(std::move(toast));
        }
    }
    ImGui::EndDisabled();
}
