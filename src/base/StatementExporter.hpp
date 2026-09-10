#pragma once

#include "ProcessingRun.hpp"
#include <string>

/**
 * @file StatementExporter.hpp
 * @brief Abstract interface for exporting a Manifold task-level statement.
 *
 * Manifold supports exporting task reports in multiple formats. Each concrete
 * statement format is implemented by a derived class that follows this
 * interface.
 *
 * @note The term "Statement" is intentionally used for the whole task report
 *       instead of "Report" so that it remains distinct from per-file report
 *       objects such as the per-file report abstraction. In other words,
 *       a Statement describes the complete processing run or task output,
 *       while a Report focuses on a lower-level file record.
 */

/**
 * @brief Abstract base class for all statement exporters.
 *
 * A `BaseStatementExporter` defines the common contract used by concrete
 * exporters to convert a `ProcessingRun` into a human-readable or machine-
 * readable task-level statement. Different output formats such as HTML,
 * plain text, JSON, or other structured formats should be implemented in
 * subclasses.
 */
class BaseStatementExporter
{
public:
    /**
     * @brief Generate a statement string for a single processing run.
     *
     * Derived classes must implement this operation to serialize the supplied
     * `ProcessingRun` instance into the target export format.
     *
     * @param run The processing run to be represented as a task statement.
     * @return A string containing the generated statement body.
     */
    virtual std::string generateStatement(const std::shared_ptr<ProcessingRun> run) = 0;

    /**
     * @brief Convert a file run status enum into a display label.
     *
     * This helper provides a canonical status label for UI rendering or
     * exporting a status summary in a statement.
     *
     * @param st The file run status value to translate.
     * @return A stable printable label for the provided status.
     */
    virtual const char* getStatusLabel(FileRunRecord::Status st);

    /**
     * @brief Fetch context information tied to a file record.
     *
     * The returned string assembles context details such as the node
     * progress, the error message, or the elapsed time associated with the
     * supplied file-level record.
     *
     * @param record The file record whose context should be summarized.
     * @return A human-readable context description for the record.
     */
    virtual std::string fetchContextInfoColumn(std::shared_ptr<FileRunRecord> record);
};
