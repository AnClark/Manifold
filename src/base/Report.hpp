#pragma once

#include <string>

/**
 * @brief Abstract base class for all per-file processing reports.
 *
 * A Report is produced by a reporting node (e.g. LoudnessComplianceNode)
 * during per-file execution and stored in FileRunRecord::reports.
 *
 * Reports are written on the worker thread and read on the UI thread;
 * access must be guarded by FileRunRecord::progressMutex.
 */
class Report {
public:
    virtual ~Report() = default;

    /** @brief ID of the node that produced this report (matches Node::id()). */
    virtual std::string nodeId() const = 0;

    /** @brief Single-line human-readable summary for compact display. */
    virtual std::string summary() const = 0;

    /**
     * @brief Overall pass/fail result for badge display.
     *
     * Returns true when all criteria evaluated by this report passed.
     * Nodes that do not have a binary pass/fail concept should return true.
     */
    virtual bool passed() const { return true; }
};
