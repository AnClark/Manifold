#include "Main.hpp"

#include "imgui.h"
#include "ImGuiNotify_MOD.hpp"
#include "utils/TableMinColumnWidth.hpp"

#include "../fonts/IconFontAwesome5_Unique.h"    // For "✗" mark

#include <chrono>
#include <cstdio>
#include <ctime>
#include <string>

// ============================================================
// Internal utilities
// ============================================================

namespace {

// Format a RunTimePoint as "YYYY-MM-DD HH:MM:SS"
static std::string fmtRunTimestamp(const RunTimePoint& tp)
{
    auto t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm_buf{};
#ifdef _WIN32
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif
    char buf[24];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d  %02d:%02d:%02d",
                  tm_buf.tm_year + 1900, tm_buf.tm_mon + 1, tm_buf.tm_mday,
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return buf;
}

// Format elapsed wall-clock duration between two time points
static std::string fmtElapsed(const RunTimePoint& start, const RunTimePoint& end)
{
    using namespace std::chrono;
    auto secs = duration_cast<seconds>(end - start).count();
    if (secs < 60)
    {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%llds", static_cast<long long>(secs));
        return buf;
    }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%lldm %llds",
                  static_cast<long long>(secs / 60),
                  static_cast<long long>(secs % 60));
    return buf;
}

static ImVec4 statusColor(FileRunRecord::Status st)
{
    switch (st)
    {
        case FileRunRecord::Status::Processing: return {1.00f, 0.80f, 0.20f, 1.0f}; // Amber
        case FileRunRecord::Status::Done:       return {0.40f, 1.00f, 0.40f, 1.0f}; // Green
        case FileRunRecord::Status::Error:      return {1.00f, 0.40f, 0.40f, 1.0f}; // Red
        case FileRunRecord::Status::Cancelled:  return {0.70f, 0.70f, 0.70f, 1.0f}; // Grey
        default:                                return ImGui::GetStyle().Colors[ImGuiCol_TextDisabled];
    }
}

static const char* statusLabel(FileRunRecord::Status st)
{
    switch (st)
    {
        case FileRunRecord::Status::Processing: return " RUN ";
        case FileRunRecord::Status::Done:       return "  OK ";
        case FileRunRecord::Status::Error:      return " ERR ";
        case FileRunRecord::Status::Cancelled:  return " CXL ";
        default:                                return " ... ";
    }
}

} // anonymous namespace

// ============================================================
// ManifoldApp::UI_Tasks
// ============================================================

void ManifoldApp::UI_Tasks()
{
    if (!ImGui::BeginChild("Tasks"))
    {
        ImGui::EndChild();
        return;
    }

    // Auto-select the latest run when first entering this panel
    if (tasksUI.selectedRunIdx < 0 && !processingRuns.empty())
        tasksUI.selectedRunIdx = static_cast<int>(processingRuns.size()) - 1;

    // ── Two-column layout: run list (left) | file table (right) ─────────
    constexpr ImGuiTableFlags layoutFlags =
        ImGuiTableFlags_SizingStretchProp |
        ImGuiTableFlags_Resizable         |
        ImGuiTableFlags_ContextMenuInBody;

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(8.0f, 2.0f));

    if (!ImGui::BeginTable("Tasks_Layout", 2, layoutFlags, ImGui::GetContentRegionAvail()))
    {
        ImGui::PopStyleVar();
        ImGui::EndChild();
        return;
    }

    ImGui::TableSetupColumn("##run_list",  ImGuiTableColumnFlags_WidthStretch, preferences.uiPref.tasksLeftPanelWeight);
    ImGui::TableSetupColumn("##file_table", ImGuiTableColumnFlags_WidthStretch, preferences.uiPref.tasksRightPanelWeight);

    // Save current column width weights to preferences storage
    const float tableWidth = ImGui::GetCurrentTable()->ColumnsAutoFitWidth;
    preferences.uiPref.tasksLeftPanelWeight = ImGui::GetCurrentTable()->Columns[0].WidthGiven / tableWidth;
    preferences.uiPref.tasksRightPanelWeight = ImGui::GetCurrentTable()->Columns[1].WidthGiven / tableWidth;

    ImGui::TableNextRow();

    // ================================================================
    // LEFT PANEL — Run list
    // ================================================================
    ImGui::TableSetColumnIndex(0);
    ImGui::SeparatorText("Run History");

    const float bottomBarHeight = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemSpacing.y;
    if (ImGui::BeginChild("Tasks_RunList", ImVec2(0, -bottomBarHeight)))
    {
        if (processingRuns.empty())
        {
            ImGui::BeginGroup();

            // Center the placeholder message vertically
            const float avail = ImGui::GetContentRegionAvail().y;
            ImGui::Dummy(ImVec2(0.0f, avail * 0.435f));

            // Align placeholder message to an appropriate position, horizontally
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x * 0.1f);
            {
                ImGui::BeginGroup();

                ImGui::TextDisabled("No processing runs yet.");
                ImGui::TextDisabled("Go to ");
                ImGui::SameLine(0, 0);
                if (ImGui::TextLink("Actions"))
                    uiState = pUIActions;
                ImGui::SameLine(0, 0);
                ImGui::TextDisabled(" and click \"Process All Files\".");

                ImGui::EndGroup();
            }

            ImGui::EndGroup();
        }
        else
        {
            // Display newest run first
            for (int i = static_cast<int>(processingRuns.size()) - 1; i >= 0; --i)
            {
                const auto& run      = processingRuns[i];
                const bool  selected = (tasksUI.selectedRunIdx == i);
                const bool  active   = !run->isComplete();

                const size_t total     = run->records.size();
                const size_t done      = run->countByStatus(FileRunRecord::Status::Done);
                const size_t err       = run->countByStatus(FileRunRecord::Status::Error);
                const size_t cancelled = run->countByStatus(FileRunRecord::Status::Cancelled);

                ImGui::PushID(i);

                // ── Card body ─────────────────────────────────────────────
                constexpr float cardHeight = 68.0f;
                const float     startY     = ImGui::GetCursorPosY();

                // Draw invisible Selectable as the interactive background
                {
                    char selId[32];
                    std::snprintf(selId, sizeof(selId), "##run%d", i);

                    if (active)
                    {
                        // Tint the card background to indicate an ongoing run
                        ImGui::PushStyleColor(ImGuiCol_Header,
                            ImVec4(0.18f, 0.36f, 0.55f, 0.85f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,
                            ImVec4(0.24f, 0.44f, 0.64f, 0.90f));
                        ImGui::PushStyleColor(ImGuiCol_HeaderActive,
                            ImVec4(0.30f, 0.52f, 0.72f, 1.00f));
                    }

                    ImGui::Selectable(selId, selected,
                        ImGuiSelectableFlags_AllowOverlap,
                        ImVec2(ImGui::GetContentRegionAvail().x, cardHeight));

                    if (active) ImGui::PopStyleColor(3);

                    if (ImGui::IsItemClicked())
                        tasksUI.selectedRunIdx = i;
                }

                // ── Overlay content inside the card ───────────────────────
                const float indent = 8.0f;
                ImGui::SetCursorPosY(startY + 5.0f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);

                // Line 1: Run ID + timestamp
                {
                    char header[80];
                    std::snprintf(header, sizeof(header), "Run #%llu   %s",
                                run->id, fmtRunTimestamp(run->timestampCreated).c_str());
                    if (active)
                        ImGui::TextColored(ImVec4(0.60f, 0.85f, 1.0f, 1.0f), "%s", header);
                    else
                        ImGui::Text("%s", header);
                }

                // Line 2: Progress bar
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                ImGui::ProgressBar(run->getProgress(),
                    ImVec2(ImGui::GetContentRegionAvail().x - indent, 5.0f), "");
                ImGui::PopStyleVar();

                // Line 3: Summary stats
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2.0f);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
                {
                    char stats[80];
                    if (active)
                        std::snprintf(stats, sizeof(stats),
                                    "Processing...  %zu / %zu done", done + err, total);
                    else if (cancelled > 0 && err > 0)
                        std::snprintf(stats, sizeof(stats),
                                    "%zu files  |  %zu OK  |  %zu error(s)  |  %zu cancelled", total, done, err, cancelled);
                    else if (cancelled > 0)
                        std::snprintf(stats, sizeof(stats),
                                    "%zu files  |  %zu OK  |  %zu cancelled", total, done, cancelled);
                    else if (err > 0)
                        std::snprintf(stats, sizeof(stats),
                                    "%zu files  |  %zu OK  |  %zu error(s)", total, done, err);
                    else
                        std::snprintf(stats, sizeof(stats),
                                    "%zu files  |  All done", total);
                    ImGui::TextDisabled("%s", stats);
                }

                // Restore cursor below the card
                ImGui::SetCursorPosY(startY + cardHeight);

                // ── Tooltip: node chain snapshot ──────────────────────────
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal | ImGuiHoveredFlags_NoSharedDelay))
                {
                    ImGui::BeginTooltip();
                    ImGui::Text("Output: %s", run->outputDir.c_str());
                    if (!run->nodeChainSnapshot.empty())
                    {
                        ImGui::Separator();
                        ImGui::TextDisabled("Node chain:");
                        for (size_t k = 0; k < run->nodeChainSnapshot.size(); ++k)
                            ImGui::Text("  [%02zu] %s", k, run->nodeChainSnapshot[k].c_str());
                    }
                    ImGui::EndTooltip();
                }

                ImGui::Dummy(ImVec2(0.0f, 4.0f));   // small gap between cards
                ImGui::PopID();
            }

            // Add an extra gap below run history, to avoid the bottom bar overlap with list item
            ImGui::Dummy(ImVec2(0.0f, 30.0f));
        }


    }
    ImGui::EndChild(); // Tasks_RunList

    // Bottom bar of Tasks_RunList
    ImGui::BeginDisabled(processingRuns.empty());
    if (ImGui::Button("Clear all completed run history", ImVec2(ImGui::GetContentRegionAvail().x, 0)))
        ImGui::OpenPopup("##clear_history_confirm");
    ImGui::EndDisabled();

    // 确认对话框
    if (ImGui::BeginPopupModal("##clear_history_confirm", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::Text("Are you sure to clear all run histories which had been done?");
        ImGui::Separator();

        if (ImGui::Button("Remove", ImVec2(120, 0)))
        {
            uint32_t erasedCount = 0;
            for (int i = 0; i < processingRuns.size(); i++)
            {
                if (processingRuns[i]->isComplete())
                {
                    processingRuns.erase(processingRuns.begin() + i);
                    erasedCount++;
                }
            }

            if (erasedCount)
                ImGui::InsertNotification({ImGuiToastType::Info, 5000, "Cleared %d completed run history.", erasedCount});
            else if (erasedCount == 0 && processingRuns.empty())
                ImGui::InsertNotification({ImGuiToastType::Warning, 5000, "Run history is empty. No history cleaned."});
            else
                ImGui::InsertNotification({ImGuiToastType::Warning, 5000, "All tasks are running. No history cleaned."});

            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();

        if (ImGui::Shortcut(ImGuiKey_Escape))
            ImGui::CloseCurrentPopup();

        if (ImGui::Button("Cancel", ImVec2(120, 0)))
            ImGui::CloseCurrentPopup();

        ImGui::EndPopup();
    }

    // ================================================================
    // RIGHT PANEL — File table for the selected run
    // ================================================================
    ImGui::TableSetColumnIndex(1);

    if (ImGui::BeginChild("Tasks_FileTable", ImVec2(0, 0)))
    {
        // Clamp selectedRunIdx in case runs were somehow removed
        if (tasksUI.selectedRunIdx < 0 ||
            tasksUI.selectedRunIdx >= static_cast<int>(processingRuns.size()))
        {
            ImGui::BeginGroup();

            // Center the placeholder message vertically
            const float avail = ImGui::GetContentRegionAvail().y;
            ImGui::Dummy(ImVec2(0.0f, avail * 0.45f));

            const char* msg = processingRuns.empty() 
                                ? "Details will show here when you select a run on the left."
                                : "Select a run on the left to view details.";
            const float msgWidth = ImGui::CalcTextSize(msg).x;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x * 0.5f - msgWidth * 0.5f);
            ImGui::TextDisabled("%s", msg);

            ImGui::EndGroup();
        }
        else
        {
            const auto& run  = processingRuns[tasksUI.selectedRunIdx];
            const bool active = !run->isComplete();

            // ── Panel header ─────────────────────────────────────────
            {
                char title[64];
                std::snprintf(title, sizeof(title), "Run #%llu", run->id);
                ImGui::SeparatorText(title);
            }

            // Run-level meta row
            {
                ImGui::AlignTextToFramePadding();
                ImGui::TextDisabled("Created: %s",
                    fmtRunTimestamp(run->timestampCreated).c_str());

                // Cancel button — right-aligned, shown only on active runs
                if (active)
                {
                    ImGui::SameLine();

                    constexpr float btnW = 110.0f;
                    constexpr float extraPadding = 10.0f;
                    const bool cancelling = run->cancelRequested.load(std::memory_order_relaxed);

                    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btnW - extraPadding);
                    ImGui::BeginDisabled(cancelling);
                    if (!cancelling)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.65f, 0.15f, 0.15f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.80f, 0.20f, 0.20f, 1.0f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.90f, 0.25f, 0.25f, 1.0f));
                    }
                    if (ImGui::Button(cancelling ? "Cancelling..." : "Cancel", ImVec2(btnW, 0)))
                        run->requestCancel();
                    if (!cancelling)
                        ImGui::PopStyleColor(3);
                    ImGui::EndDisabled();
                }

                if (!active)
                {
                    // Find the actual finish time: latest timestampFinished among records
                    RunTimePoint latestFinish = run->timestampCreated;
                    for (const auto& r : run->records)
                    {
                        // Only Done/Error records have valid timestampFinished
                        auto st = r->status.load(std::memory_order_relaxed);
                        if (st == FileRunRecord::Status::Done ||
                            st == FileRunRecord::Status::Error)
                        {
                            if (r->timestampFinished > latestFinish)
                                latestFinish = r->timestampFinished;
                        }
                    }
                    ImGui::SameLine(0, 20);
                    ImGui::TextDisabled("Elapsed: %s",
                        fmtElapsed(run->timestampCreated, latestFinish).c_str());
                }

                ImGui::TextDisabled("Output: %s", run->outputDir.c_str());
            }

            // Summary progress bar (shown only while active)
            if (active)
            {
                ImGui::Spacing();
                const size_t done  = run->countByStatus(FileRunRecord::Status::Done);
                const size_t err   = run->countByStatus(FileRunRecord::Status::Error);
                const size_t total = run->records.size();
                char overlay[32];
                std::snprintf(overlay, sizeof(overlay), "%zu / %zu", done + err, total);
                ImGui::ProgressBar(run->getProgress(), ImVec2(-1.0f, 0.0f), overlay);
                ImGui::Spacing();
            }
            else
            {
                ImGui::Spacing();
            }

            // ── File table ────────────────────────────────────────────
            constexpr ImGuiTableFlags tblFlags =
                ImGuiTableFlags_RowBg              |
                ImGuiTableFlags_BordersInnerV      |
                ImGuiTableFlags_SizingFixedFit     |
                ImGuiTableFlags_Resizable          |
                ImGuiTableFlags_ScrollX            |
                ImGuiTableFlags_ScrollY;

            // Populate actual column count (basic columns + report columns)
            run->populateReportSources();
            constexpr int s_basicColumns  = 3;
            const int     s_reportColumns = run->reportSrcsPopulated.size();

            if (ImGui::BeginTable("Tasks_Files", s_basicColumns + s_reportColumns, tblFlags))
            {
                ImGui::TableSetupScrollFreeze(0, 1);
                // TODO: Store tasksFilesColumnWidths in vector rather than array
                ImGui::TableSetupColumn("File",         ImGuiTableColumnFlags_WidthFixed, preferences.uiPref.tasksFilesColumnWidths[0]);
                ImGui::TableSetupColumn("Status",       ImGuiTableColumnFlags_WidthFixed, preferences.uiPref.tasksFilesColumnWidths[1]);
                ImGui::TableSetupColumn("Info",         ImGuiTableColumnFlags_WidthFixed, preferences.uiPref.tasksFilesColumnWidths[2]);
                if (s_reportColumns)
                {
                    // NOTE: The content of run->reportSrcIds is synced with run->reportSrcsPopulated
                    for (const auto& reportSrc : run->reportSrcNames)
                    {
                        ImGui::TableSetupColumn(reportSrc.data(), ImGuiTableColumnFlags_WidthFixed, 200.0f);
                    }
                }
                ImGui::TableHeadersRow();

                ImGuiListClipper clipper;
                clipper.Begin(static_cast<int>(run->records.size()));
                while (clipper.Step())
                {
                    for (int ri = clipper.DisplayStart; ri < clipper.DisplayEnd; ++ri)  // ri = record index
                    {
                        const auto& record = run->records[ri];
                        const auto  status  = record->status.load(std::memory_order_relaxed);

                        ImGui::PushID(ri);
                        ImGui::TableNextRow();

                        // Col 0 — File name
                        ImGui::TableSetColumnIndex(0);
                        {
                            ImGui::TextUnformatted(record->fileInfo->fileNameBase.c_str());

                            // Full path tooltip
                            if (ImGui::IsItemHovered(
                                    ImGuiHoveredFlags_DelayShort |
                                    ImGuiHoveredFlags_NoSharedDelay))
                            {
                                ImGui::SetTooltip("%s", record->fileInfo->filePath.c_str());
                            }
                        }

                        // Col 1 — Status badge
                        ImGui::TableSetColumnIndex(1);
                        {
                            ImGui::TextColored(statusColor(status), "%s", statusLabel(status));
                        }

                        // Col 2 — Context info (node progress / error message / elapsed)
                        ImGui::TableSetColumnIndex(2);
                        {
                            if (status == FileRunRecord::Status::Processing)
                            {
                                std::scoped_lock<std::mutex> rlock(record->progressMutex);
                                if (!record->currentNodeName.empty())
                                {
                                    char prog[128];
                                    std::snprintf(prog, sizeof(prog), "[%02zu] %s",
                                                record->currentNodeIndex,
                                                record->currentNodeName.c_str());
                                    ImGui::TextDisabled("%s", prog);
                                }
                            }
                            else if (status == FileRunRecord::Status::Error)
                            {
                                std::scoped_lock<std::mutex> rlock(record->progressMutex);
                                if (!record->errorMessage.empty())
                                {
                                    ImGui::TextColored(statusColor(status), "!");
                                    ImGui::SameLine();
                                    ImGui::BeginDisabled();
                                    ImGui::TextWrapped("%s", record->errorMessage.c_str());
                                    ImGui::EndDisabled();

                                    // Full error in tooltip (message may be long)
                                    if (ImGui::IsItemHovered(
                                            ImGuiHoveredFlags_DelayShort |
                                            ImGuiHoveredFlags_NoSharedDelay))
                                    {
                                        ImGui::BeginTooltip();
                                        ImGui::PushTextWrapPos(400.0f);
                                        ImGui::TextUnformatted(record->errorMessage.c_str());
                                        ImGui::PopTextWrapPos();
                                        ImGui::EndTooltip();
                                    }
                                }
                            }
                            else if (status == FileRunRecord::Status::Done)
                            {
                                const std::string elapsed =
                                    fmtElapsed(record->timestampStarted, record->timestampFinished);
                                ImGui::TextDisabled("%s", elapsed.c_str());
                            }
                            else if (status == FileRunRecord::Status::Cancelled)
                            {
                                ImGui::TextDisabled("Cancelled");
                            }
                        }

                        // Col 3+ — Report view
                        for (int i = 0; i < s_reportColumns; i++)
                        {
                            ImGui::TableSetColumnIndex(s_basicColumns + i);

                            std::shared_ptr<Report> currentReport = nullptr;
                            for (const auto& report : record->reports)
                            {
                                if (report->nodeId() != run->reportSrcIds[i])
                                    continue;
                                currentReport = report;
                            }
                            if (!currentReport) continue;

                            // Cell text: prefix symbol + value, coloured by status
                            const auto   st     = currentReport->status();
                            const char*  prefix = (st == Report::Status::Pass) ? "\xe2\x9c\x93 "    // ✓
                                                : (st == Report::Status::Fail) ? ICON_FA_TIMES " "  // ✗
                                                : "";
                            const ImVec4 col = (st == Report::Status::Pass)
                                                   ? ImVec4{0.40f, 1.00f, 0.40f, 1.0f}
                                                   : (st == Report::Status::Fail)
                                                         ? ImVec4{1.00f, 0.40f, 0.40f, 1.0f}
                                                         : ImGui::GetStyle().Colors[ImGuiCol_Text];
                            ImGui::TextColored(col, "%s%s", prefix, currentReport->summary().c_str());

                            // Tooltip: full details()
                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort |
                                                     ImGuiHoveredFlags_NoSharedDelay))
                            {
                                ImGui::BeginTooltip();
                                ImGui::TextDisabled("%s", currentReport->nodeId().c_str());
                                ImGui::Separator();
                                ImGui::PushTextWrapPos(480.0f);
                                ImGui::TextUnformatted(currentReport->details().c_str());
                                ImGui::PopTextWrapPos();
                                ImGui::EndTooltip();
                            }
                        }

                        ImGui::PopID();
                    }
                }
                clipper.End();

                // Save custom column widths
                for (uint8_t i = 0; i < 4; i++)
                    preferences.uiPref.tasksFilesColumnWidths[i] = ImGui::GetCurrentTable()->Columns[i].WidthGiven;

                ImGui::EndTable();
            }
        }
    }
    ImGui::EndChild(); // Tasks_FileTable

    // HACK: Set minimum column width
    // Make sure reference text (for example, the tip text in Details view) can be fully shown.
    constexpr const char* referenceText = "Details will show here when you select a run on the left.";
    const float referenceTextWidth = ImGui::CalcTextSize(referenceText).x;
    const float minColumnWidth = referenceTextWidth + ImGui::GetStyle().CellPadding.x * 2.0f;
    ImGuiHack::SetTableMinColumnWidth(minColumnWidth);

    // Apply minimum column width to avoid the panel getting too narrow when resizing window
    // (To test, maximize the window, dragging it to the minimum width, then restore the window)
    if (ImGui::GetCurrentTable()->Columns[0].WidthGiven < minColumnWidth)
        ImGui::TableSetColumnWidth(0, minColumnWidth);

    ImGui::EndTable();
    ImGui::PopStyleVar();
    ImGui::EndChild(); // Tasks
}
