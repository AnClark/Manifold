#include "HTMLStatementExporter.hpp"
#include "utils/LogManager.hpp"
#include "utils/Timestamp.hpp"
#include "utils/StringUtils.hpp"

#include <inja.hpp>
using json = nlohmann::json;

/**
 TODOS:
   - Hover file name for full path
 */

static const std::string TEMPLATE_STR = R"(
<!DOCTYPE html charset="utf-8">
<html>
<head>
    <title>{{ page_title }}</title>
    <style>
        /* ==============================================================
         * Global style & Main elements' styles
         * ==============================================================
         */

        body {
            font-family: -apple-system, "Segoe UI", "PingFang SC", "Microsoft YaHei", sans-serif;
            background: #fff;
            color: #1a1a1a;
        }

        .main-title {
            font-weight: 500;
        }
        
        .metadata-view {
            font-size: 14px;
            color: #1a1a1aa9;
        }

        /* ==============================================================
         * Statement Content (statement-card)
         * ==============================================================
         */

        /* Statement display area (top-level container) */
        .statement-card {
            border: 1px solid #e8e8e8;
            border-radius: 12px;
            overflow: hidden;          /* Allow rounded corners to clip the table content */
            background: #fff;
            max-width: 100%;
        }

        /* Scroll area: table size is limited here */
        .statement-scroll {
            max-height: 480px;         /* When overflow, content will scroll within the table */
            overflow-y: auto;
            overflow-x: auto;
        }

        .statement-table {
            width: max-content;         /* KEY POINT 1: Let the content furfill the width */
            min-width: 100%;            /* At least furfill the width even if too less content */
            table-layout: fixed;        /* KEY POINT 2: Determine the column width with the header row */
            border-collapse: separate; /* Do not use "collapse" mode, otherwise sticky header will misplace */
            border-spacing: 0;
            font-size: 14px;
        }

        /* Header: sticks to the top while scrolling */
        .statement-table thead th {
            position: sticky;
            top: 0;
            z-index: 2;
            background: #f7f7f7;
            color: #333;
            font-weight: 600;
            text-align: left;
            padding: 14px 24px;
            border-bottom: 1px solid #ececec;
            white-space: nowrap;
        }

        /* Data rows */
        .statement-table tbody td {
            padding: 14px 24px;
            border-bottom: 1px solid #f2f2f2;
            vertical-align: top;
        }
        .statement-table tbody tr:last-child td {
            border-bottom: none;
        }
        .statement-table tbody tr:hover td {
            background: #fafafa;
        }

        /* Column widths */
        .statement-table th, .statement-table td {
            overflow-wrap: anywhere;    /* Word wrap on long word & path */
        }

        .statement-table th:nth-child(1), .statement-table td:nth-child(1) { max-width: 280px; }
        .statement-table th:nth-child(2), .statement-table td:nth-child(2) { max-width: 100px; }
        .statement-table th:nth-child(3), .statement-table td:nth-child(3) { max-width: 300px; }
        .statement-table td:nth-child(3) { overflow-wrap: anywhere; }

        /* Dynamic appended columns: Specify a width limit, otherwise they will devide the space equally */
        .statement-table th:nth-child(n+4), .statement-table td:nth-child(n+4) { max-width: 28em; }
        .statement-table td:nth-child(n+4) { overflow-wrap: anywhere; }

        /* Tweak: Fade out the bottom of scroll area to indicate users there are still rows beyond scroll area */
        .statement-card {
            position: relative;                 /* Work with overflow:hidden in .statement-card */
        }
        .statement-card::after {
            content: "";
            position: absolute;
            left: 0; right: 0; bottom: 0;
            height: 2.5em;
            background: linear-gradient(to bottom, rgba(255,255,255,0), #fff);
            pointer-events: none;
        }
        
        /* ==============================================================
         * CSS Tooltip Text
         * ==============================================================
         */

        /* 触发元素 */
        .tip {
            color: #3b82f6;
            position: relative;        /* 定位基准 */
            cursor: pointer;
        }

        /* 气泡内容：直接写在 HTML 里的 <span> */
        .tip .tooltip-text {
            position: absolute;
            bottom: 130%;              /* 位于触发元素上方 */
            left: 50%;
            transform: translateX(-50%);

            background: #1a1a1a;
            color: #fff;
            padding: 8px 16px;
            border-radius: 8px;
            font-size: 14px;
            white-space: nowrap;

            opacity: 0;
            visibility: hidden;
            pointer-events: none;
            transition: opacity 0.2s ease;
            z-index: 10;
        }

        /* 气泡的三角箭头：这里不再需要放内容，箭头本身也没有文字 */
        .tip .tooltip-text::after {
            content: "";               /* 三角是装饰性的，content 留空即可 */
            position: absolute;
            top: 100%;                 /* 紧贴气泡底部 */
            left: 50%;
            transform: translateX(-50%);
            border: 6px solid transparent;
            border-top-color: #1a1a1a;
            border-bottom: 0;
        }

        /* 悬停显示 */
        .tip:hover .tooltip-text,
        .tip:focus-visible .tooltip-text {   /* 键盘聚焦也显示，提升无障碍 */
            opacity: 1;
            visibility: visible;
        }

        /* 【特殊处理】第一列 / 左边缘：左对齐，气泡从左边缘开始展开 */
        .tip .tooltip-text--start {
            left: 0;
            right: auto;
            transform: none;
        }
        .tip .tooltip-text--start::after {
            left: 20px;        /* 三角箭头跟着挪 */
            right: auto;
            transform: none;
        }

        /* 【特殊处理】右边缘：右对齐 */
            .tip .tooltip-text--end {
            right: 0;
            left: auto;
            transform: none;
        }
        .tip .tooltip-text--end::after {
            right: 20px;
            left: auto;
            transform: none;
        }
    </style>
</head>
<body>
    <h1 class="main-title">Manifold Run Report</h1>
    <div class="metadata-view">
        <p>Run at: {{ run_timestamp }}</p>
    </div>
    <div class="statement-card">
        <div class="statement-scroll">
            <table class="statement-table">
                <thead>
                    <tr>
                        <th>File</th>
                        <th>Status</th>
                        <th>Info</th>
                        {% for report_column_header in report_column_headers %}
                            <th>{{ report_column_header }}</th>
                        {% endfor %}
                    </tr>
                </thead>
                <tbody>
                    {% for record in records %}
                        <tr>
                            <td>
                                <span class="tip" tabindex="0" role="tooltip">
                                {{ record.file_name }}
                                <span class="tooltip-text tooltip-text--start">{{ record.file_path }}</span>
                                </span>
                            </td>
                            <td>{{ record.status }}</td>
                            <td>{{ record.info }}</td>
                            {% for report in record.reports %}
                            <td>{{ report.details }}</td>
                            {% endfor %}
                        </tr>
                    {% endfor %}
                </tbody>
            </table>
        </div>
    </div>

</body>
</html>
)";

std::string HTMLStatementExporter::generateStatement(const std::shared_ptr<ProcessingRun> run)
{
    //
    // BUILD JSON TEMPLATE CONTEXT
    //
    json data;

    // 1. Page title & timestamp
    char pageTitle[256];
    std::string rumTimestamp = TimestampUtils::fmtRunTimestamp(run->timestampCreated);

    snprintf(pageTitle, 256, "Manifold Run Report - %s", rumTimestamp.c_str());

    data["page_title"] = pageTitle;
    data["run_timestamp"] = rumTimestamp;

    // 2. Per-file report column's header (analyzer results, etc.)
    //    See: UI_Tasks.cpp
    data["report_column_headers"] = {};
    for (const auto& col : run->reportColDefs)
    {
        data["report_column_headers"].emplace_back(col.header().c_str());
    }

    // 3. Statement records
    const auto records = run->records;
    data["records"] = {};
    for (auto index = 0; index < records.size(); index++)
    {
        // Column 1/2/3: File, Status, Info
        const auto record = records[index];
        data["records"].push_back({
            {"file_name", record->fileInfo->fileNameBase},
            {"file_path", record->fileInfo->filePath},
            {"status", getStatusLabel(record->status)},
            {"info", StringUtils::convertNewlineToBr(fetchContextInfoColumn(record))},
            {"reports", json::array()}
        });

        // Column 4+: Reports
        for (int i = 0; i < run->reportColDefs.size(); i++)
        {
            // Find the occurrenceIdx-th report with the matching nodeId
            const auto& colDef = run->reportColDefs[i];
            std::shared_ptr<Report> currentReport = nullptr;
            {
                int occ = 0;
                for (const auto& report : record->reports)
                {
                    if (report->nodeId() != colDef.nodeDef->id) continue;
                    if (occ == colDef.occurrenceIdx) { currentReport = report; break; }
                    ++occ;
                }
            }

            auto& reportNode = data["records"][index]["reports"];
            if (!currentReport)
            {
                // If no report: setup a placeholder (empty string)
                reportNode.push_back({
                    {"summary", std::string()},
                    {"details", std::string()}
                });
                continue;                
            }
            else
            {
                // If there is a report: render it
                reportNode.push_back({
                    {"summary", StringUtils::convertNewlineToBr(currentReport->summary()) },
                    {"details", StringUtils::convertNewlineToBr(currentReport->details()) }
                });                
            }
        }
    }

    //
    // RENDER OUTPUT
    //
    std::string htmlOutput;
    this->errMessage = "";
    try
    {
        htmlOutput = inja::render(TEMPLATE_STR, data);
        LOG_DEBUGF("HTMLStatementExporter", "HTML Statement generated. Length: %d", htmlOutput.length());
    }
    catch (const std::runtime_error& err)
    {
        this->errMessage = err.what();
        LOG_ERRORF("HTMLStatementExporter", "HTML Statement generation failure: %s", this->errMessage.c_str());
    }

    return htmlOutput;
}
