#pragma once

#include "base/StatementExporter.hpp"

/**
 * @file HTMLStatementExporter.hpp
 * @brief Concrete HTML implementation of the statement exporter interface.
 *
 * This exporter produces an HTML-formatted task statement for a Manifold
 * processing run. The name "Statement" is used intentionally instead of
 * "Report" because the exported artifact is associated with the overall task
 * or processing run, rather than with an individual per-file report object.
 * The exporter therefore follows the task-level statement concept defined by
 * the abstract interface in the base API.
 */

/**
 * @brief HTML-backed implementation of `BaseStatementExporter`.
 *
 * The HTML exporter serializes a `ProcessingRun` into a string that carries
 * the HTML markup for the generated task statement. It also exposes a small
 * error-tracking surface so callers can inspect whether the generator has
 * encountered a formatting or export problem.
 */
class HTMLStatementExporter : public BaseStatementExporter
{
private:
    std::string errMessage {""};

public:
    /**
     * @brief Generate an HTML statement for the supplied processing run.
     *
     * @param run The task-level processing run to serialize.
     * @return A string containing the HTML statement output.
     */
    std::string generateStatement(const std::shared_ptr<ProcessingRun> run) override;

    /**
     * @brief Retrieve the most recent exporter error message (comes from
     *        Inja template engine).
     *
     * @return A non-empty error message when generation has failed; empty
     *         otherwise.
     */
    std::string getErrorMessage() const { return errMessage; }

    /**
     * @brief Check whether the exporter has recorded an error message
     *        (comes from Inja template engine).
     *
     * @return `true` if `errMessage` is non-empty, `false` otherwise.
     */
    bool hasError() { return !errMessage.empty(); }
};
