#pragma once

#include "base/StatementExporter.hpp"

/**
 * @file XLSXStatementExporter.hpp
 * @brief Concrete XLSX implementation of the statement exporter interface.
 *
 * This header defines the XLSX-backed statement exporter used by Manifold to
 * produce a spreadsheet-style task statement from a `ProcessingRun`. The
 * terminology "Statement" is intentionally used for the task-level export
 * rather than "Report" so that it remains distinct from per-file report
 * artifacts.
 */

/**
 * @brief XLSX-backed implementation of `BaseStatementExporter`.
 *
 * The `XLSXStatementExporter` serializes a `ProcessingRun` into an XLSX
 * statement output, using the shared interface contract defined in the base
 * statement exporter API. The class also keeps a lightweight error message
 * buffer for template or document-generation failures.
 */
class XLSXStatementExporter : public BaseStatementExporter
{
private:
    std::string errMessage {""};

public:
    /**
     * @brief Generate an XLSX statement for the supplied processing run.
     *
     * @param run The task-level processing run to serialize.
     * @return A string or byte buffer representation produced by the
     *         concrete exporter implementation.
     */
    std::string generateStatement(const std::shared_ptr<ProcessingRun> run) override;

    /**
     * @brief Retrieve the most recent exporter error message.
     *
     * When the XLSX template or document generation path fails, the exporter
     * may store the originating engine error in `errMessage`.
     *
     * @return A non-empty error message when generation has failed; empty
     *         otherwise.
     */
    std::string getErrorMessage() const { return errMessage; }

    /**
     * @brief Check whether the exporter has recorded an error message.
     *
     * @return `true` if `errMessage` is non-empty, `false` otherwise.
     */
    bool hasError() { return !errMessage.empty(); }
};
