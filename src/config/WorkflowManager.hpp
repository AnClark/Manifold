#pragma once

#include <vector>
#include <string>
#include <filesystem>

// Workflow: A Node Chain which has been stored in Manifold data directory for easily re-using,
//           just like "presets" / "programs" in audio plugins.

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

private:
    std::vector<WorkflowDescriptor> workflows;

    int _scanWorkflows(std::filesystem::path path, std::vector<WorkflowDescriptor>& target, bool clearContainer = false);    
};
