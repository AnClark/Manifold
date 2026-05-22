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

    /**
     * @brief Outcome category used for badge display in the UI.
     *
     *   Pass — all criteria met            → green  ● PASS badge
     *   Fail — one or more criteria failed → red    ● FAIL badge
     *   Info — informational metric only,
     *           no binary pass/fail gate   → blue   ● INFO badge
     */
    enum class Status { Pass, Fail, Info };

    /** @brief ID of the node that produced this report (matches Node::id()). */
    virtual std::string nodeId() const = 0;

    /** @brief Single-line human-readable summary for compact display. */
    virtual std::string summary() const = 0;

    /**
     * @brief Outcome status for badge display.
     *
     * Override to return Fail when criteria are not met, or Info for purely
     * informational metrics that have no pass/fail gate.
     * The default implementation returns Pass.
     */
    virtual Status status() const { return Status::Pass; }

    /**
     * @brief Convenience accessor: returns true unless status() is Fail.
     *
     * Preserved for call sites that only need a binary result.
     * Prefer status() in new code when the Info state matters.
     */
    bool passed() const { return status() != Status::Fail; }
};
