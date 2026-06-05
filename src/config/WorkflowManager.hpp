#pragma once

#include <vector>
#include <string>
#include <filesystem>

// Workflow: A Node Chain which has been stored in Manifold data directory for easily re-using,
//           just like "presets" / "programs" in audio plugins.

/** Carries a human-readable message produced during a rescan (e.g. a file that was
 *  automatically renamed to resolve a duplicate-name conflict). The UI drains these
 *  each frame and shows them as toast notifications. */
struct ScanNotification {
    std::string message;
};

struct WorkflowDescriptor {
    std::string             name;    
    std::filesystem::path   filePath;
    std::string             description;  
    std::string             group;

    bool parseMetadata();
};

class WorkflowManager
{
public:
    WorkflowManager()
    {
        scanUserWorkflow();
    }

    void scanUserWorkflow(bool rescan = false);

    std::vector<const WorkflowDescriptor*> listByGroup(std::string group) const;
    std::vector<std::string> listGroups() const;

    int workflowsCount() const { return workflows.size(); }

    /** Returns true if the in-memory list already contains a workflow with the given
     *  name inside the given group (empty group = ungrouped). Used by the UI to show
     *  an "overwrite" warning before the user hits Save. */
    bool nameExistsInGroup(const std::string& name, const std::string& group) const;

    /** Returns all pending scan notifications and clears the internal queue.
     *  Call this from the render loop to show toasts for deferred events. */
    std::vector<ScanNotification> drainScanNotifications();

    /**
     * @brief Saves the current node chain (provided as a pre-serialised TOML string)
     *        as a Workflow file in the user-data directory.
     *
     * The file is placed under `workflows/[sanitized_group]/[sanitized_name].toml`.
     * The group is encoded solely in the sub-directory name; no `group` key is
     * written inside the TOML file.
     * If a file with the derived name already exists, a HHmmss timestamp suffix is
     * appended to avoid overwriting it.  The in-memory list is refreshed after writing.
     *
     * @param name        Human-readable name; used as the TOML `name` field and as
     *                    the basis for the generated filename.
     * @param description Optional description (may be empty).
     * @param group       Logical group; used as the sub-directory name only.
     *                    Falls back to "Default" when empty.
     * @param tomlContent Fully serialised TOML text from NodeConfig::saveNodeChain().
     * @return true on success; false on any I/O error.
     */
    bool saveWorkflow(const std::string& name,
                      const std::string& description,
                      const std::string& group,
                      const std::string& tomlContent);

    /**
     * @brief Deletes the backing TOML file for @p w and refreshes the in-memory list.
     * @return true on success; false if the file could not be removed.
     */
    bool deleteWorkflow(const WorkflowDescriptor& w);

    /**
     * @brief Renames a workflow: updates the `name` field inside the TOML file and
     *        refreshes the in-memory list.  The physical filename is not changed.
     * @return true on success; false on I/O error or if @p newName is already taken
     *         in the same group.
     */
    bool renameWorkflow(const WorkflowDescriptor& w, const std::string& newName);

    /**
     * @brief Moves a workflow to a different group by moving its file to the
     *        corresponding sub-directory and refreshing the in-memory list.
     *        Empty @p newGroup = ungrouped (root workflows/ directory).
     * @return true on success; false on I/O error.
     */
    bool moveWorkflowToGroup(const WorkflowDescriptor& w, const std::string& newGroup);

private:
    std::vector<WorkflowDescriptor> workflows;
    std::vector<ScanNotification>   _scanNotifications;

    int _scanWorkflows(std::filesystem::path path, std::vector<WorkflowDescriptor>& target, bool clearContainer = false);

    /**
     * @brief Returns a filesystem-safe version of @p str by replacing characters
     *        illegal on Windows or POSIX with underscores.
     *
     * Non-ASCII UTF-8 bytes (e.g. CJK) are preserved so that paths remain
     * meaningful on all platforms.
     */
    static std::string _sanitizeForFilename(const std::string& str);
};
