#include "./Actions.hpp"
#include "Main.hpp"
#include "base/AudioFormats.hpp"
#include "../fonts/IconFontAwesome5_Unique.h"

#include <imgui.h>

#include <algorithm>
#include <filesystem>
#include <mutex>

UIComponents_Actions::UIComponents_Actions(ManifoldApp *app_) : app(app_)
{}

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