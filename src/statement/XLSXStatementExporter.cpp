#include "XLSXStatementExporter.hpp"
#include "utils/LogManager.hpp"
#include "utils/StringUtils.hpp"
#include "utils/Timestamp.hpp"

#include <xlsxwriter.h>

#include <algorithm>
#include <vector>

std::string XLSXStatementExporter::generateStatement(const std::shared_ptr<ProcessingRun> run)
{
    const char *buffer = nullptr;
    size_t bufferSize = 0;
    lxw_error errid = lxw_error::LXW_NO_ERROR;

    this->errMessage = "";

    //
    // CONFIGURE MEMORY OUTPUT MODE
    //
    lxw_workbook_options options = {};
    options.output_buffer = &buffer;
    options.output_buffer_size = &bufferSize;

    //
    // CREATE WORKBOOK IN MEMORY (by passing NULL as file name)
    //
    lxw_workbook *workbook = workbook_new_opt(NULL, &options);
    if (!workbook) {
        constexpr const char* _errMsg = "Failed to create workbook";
        LOG_ERRORF("XLSXStatementExporter", "%s", _errMsg);
        this->errMessage = _errMsg;

        return std::string();
    }

    //
    // CREATE WORKSHEET
    //
    // Sanitize and format sheet name (replace illegal char, and limit size to 31 characters)
    std::string rumTimestamp = TimestampUtils::fmtRunTimestamp(run->timestampCreated);
    for (char &c : rumTimestamp) {
        if (c == ':' || c == '/' || c == '\\' || c == '?' || c == '*' || c == '[' || c == ']') {
            c = '-'; // Replace illegal chars with '-'
        }
    }

    char sheetName[32] = {0}; // Sheet name limits to 31 charaters + terminator
    snprintf(sheetName, sizeof(sheetName), "Task  %.27s", rumTimestamp.c_str());

    lxw_worksheet *worksheet = workbook_add_worksheet(workbook, sheetName);
    if (!worksheet) {
        LOG_ERRORF("XLSXStatementExporter", "Failed to add worksheet with name: %s", sheetName);
        this->errMessage = "Failed to add worksheet";
        workbook_close(workbook);

        return std::string();
    }

    //
    // CREATE FORMAT FOR HEADER
    //
    lxw_format *header_format = workbook_add_format(workbook);
    format_set_bold(header_format);
    format_set_font_color(header_format, LXW_COLOR_WHITE);
    format_set_bg_color(header_format, 0x1F497D); // RGB: Dark blue
    format_set_align(header_format, LXW_ALIGN_CENTER);
    format_set_align(header_format, LXW_ALIGN_VERTICAL_CENTER);
    format_set_border(header_format, LXW_BORDER_THIN);

    //
    // HEADER LINE
    //    
    worksheet_write_string(worksheet, 0, 0, "File", header_format);
    worksheet_write_string(worksheet, 0, 1, "Path", header_format);
    worksheet_write_string(worksheet, 0, 2, "Status", header_format);
    worksheet_write_string(worksheet, 0, 3, "Info", header_format);

    // Per-file report column's header (analyzer results, etc.)
    // See: UI_Tasks.cpp
    uint32_t headerColumnId = 4;
    for (const auto& col : run->reportColDefs)
    {
        worksheet_write_string(worksheet, 0, headerColumnId++, col.header().c_str(), header_format);
    }

    //
    // WRITE FILE ENTRIES
    //
    // Record the largest column with based on the longest text
    std::vector<float> columnVisualWidths(headerColumnId, 10.0f);

    // Shortcut: write new entry, then update the column width to make sure the whole entry is shown
    auto writeDataLine = [&](uint32_t row, uint32_t col, const std::string& str) 
    {
        worksheet_write_string(worksheet, row, col, str.c_str(), NULL);

        float _visualWidth = static_cast<float>(StringUtils::getExcelDisplayWidth(str));
        columnVisualWidths[col] = std::max(columnVisualWidths[col], _visualWidth + 8.0f);   // Add some paddings
        worksheet_set_column(worksheet, col, col, columnVisualWidths[col], NULL);
    };

    const auto records = run->records;
    for (auto index = 0; index < records.size(); index++)
    {
        const auto record = records[index];
        const uint32_t rowId = index + 1;  // Row ID starts from 1 (Row #0 is header)

        // Column 0: File name
        //worksheet_write_string(worksheet, rowId, 0, record->fileInfo->fileNameBase.c_str(), NULL);
        writeDataLine(rowId, 0, record->fileInfo->fileNameBase);

        // Column 1: File path
        //worksheet_write_string(worksheet, rowId, 1, record->fileInfo->filePath.c_str(), NULL);
        writeDataLine(rowId, 1, record->fileInfo->filePath);

        // Column 2: Status
        //worksheet_write_string(worksheet, rowId, 2, getStatusLabel(record->status), NULL);
        writeDataLine(rowId, 2, getStatusLabel(record->status));

        // Column 3: Info
        //worksheet_write_string(worksheet, rowId, 3, fetchContextInfoColumn(record).c_str(), NULL);
        writeDataLine(rowId, 3, fetchContextInfoColumn(record).c_str());        

        // Column 4+: Reports
        for (int reportIndex = 0; reportIndex < run->reportColDefs.size(); reportIndex++)
        {
            // Find the occurrenceIdx-th report with the matching nodeId
            const auto& colDef = run->reportColDefs[reportIndex];
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

            if (!currentReport)
                continue;

            const uint32_t columnId = 4 + reportIndex;
            //worksheet_write_string(worksheet, rowId, columnId, currentReport->details().c_str(), NULL);
            writeDataLine(rowId, columnId, currentReport->details());                    
        }
    }

    //
    // SAVE XLSX TO MEMORY
    //
    // Invoke workbook_close. At this time, it fills Excel binary data to buffer
    errid = workbook_close(workbook);
    if (errid)
    {
        LOG_ERRORF("XLSXStatementExporter", "Failed while writing Excel file to memory: %s", lxw_strerror(errid));
        errMessage = std::string(lxw_strerror(errid));
        
        return std::string();
    }

    LOG_DEBUGF("XLSXStatementExporter", "Successfully generated Excel file in memory. Size: %d", bufferSize);

    //
    // FINALIZE
    //
    // Export buffer to std::string
    std::string output_buffer(buffer, bufferSize);

    // NOTICE: You MUST free the buffer memory explicitly!
    free((void*)buffer);

    // Return XLSX data as std::string
    return std::move(output_buffer);
}
