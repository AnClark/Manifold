#include "WorkflowManager.hpp"
#include "utils/GetUserDataPath.hpp"
#include "utils/LogManager.hpp"
#include "Config.hpp"

#include <algorithm>
#include <filesystem>
#include <unordered_set>

// Returns the full path to the workflow folder.
static constexpr const char* kWorkflowDirName = "workflows";

static constexpr const char* kDefaultGroup = "Default";

static constexpr const char* kNoNamePlaceholder = "(unnamed workflow)";

static std::filesystem::path preferencesFilePath()
{
    return std::filesystem::path(GetUserDataDirectory()) / kWorkflowDirName;
}

int WorkflowManager::_scanWorkflows(std::filesystem::path path, std::vector<WorkflowDescriptor>& target, bool clearContainer)
{
    static const std::unordered_set<std::string> supportedExts = {
        ".toml"
    };

    int foundFilesCount = 0;

    if (clearContainer)
        target.clear();

    std::error_code ec;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(path, ec))
    {
        if (!entry.is_regular_file(ec))
            continue;
        std::string ext = entry.path().extension().string();
        // Lowercase extension for case-insensitive comparison
        std::transform(ext.begin(), ext.end(), ext.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (supportedExts.count(ext))
        {
            WorkflowDescriptor wd;
            wd.filePath = entry.path();
            if (wd.parseMetadata())
                target.push_back(std::move(wd));
            foundFilesCount++;
        }
    }

    return foundFilesCount;
}

void WorkflowManager::scanUserWorkflow(bool rescan)
{
    int ret = _scanWorkflows(preferencesFilePath(), this->workflows, rescan);
    LOG_DEBUGF("WorkflowMgr", "Found %d workflow files in user profile directory.", ret);
}

std::vector<const WorkflowDescriptor*> WorkflowManager::listByGroup(std::string group) const
{
    std::vector<const WorkflowDescriptor*> result;
    for (const auto& w : workflows)
        if (w.group == group)
            result.push_back(&w);
    return std::move(result);
}

std::vector<std::string> WorkflowManager::listGroups() const
{
    std::vector<std::string> groups;
    for (const auto& w : workflows) {
        if (std::find(groups.begin(), groups.end(), w.group) == groups.end())
            groups.push_back(w.group);
    }
    return std::move(groups);
}

bool WorkflowDescriptor::parseMetadata()
{
    // No file yet — keep all defaults silently.
    if (!std::filesystem::exists(filePath))
    {
        LOG_ERRORF("WorkflowMgr", "Parse %s failed: file does not exist. Will not load this workflow file.");
        return false;
    }

    // Malformed file — keep all defaults silently.
    try
    {
        // Open via ifstream(filesystem::path): on Windows this calls the wide-char
        // (_wfopen) API internally, correctly handling UTF-8 / CJK paths.
        // toml::parse_file(string) uses fopen(char*) which treats the path as
        // ANSI on Windows, silently failing for non-ASCII paths.
        std::ifstream ifs(filePath, std::ios::in);
        if (!ifs.is_open())
        {
            LOG_ERRORF("WorkflowMgr", "Parse %s failed: cannot open file. Will not load this workflow file.", filePath.u8string().c_str());
            return false; // File unreadable — keep all defaults silently.
        }

        auto result = toml::parse(ifs, filePath.u8string());

        if (const auto configType = result["config_type"].value<std::string>())
        {
            LOG_DEBUGF(__func__, "configType: %s", configType->c_str());

            if (configType != kConfigTypeNodeChain)
            {
                LOG_ERRORF("WorkflowMgr", "Parse %s failed: workflow file mismatch. Expected '%s'.", filePath.u8string().c_str(), kConfigTypeNodeChain);
            }
        }

        if (const auto name_ = result["name"].value<std::string>())
            this->name = name_->c_str();
        else
            this->name = kNoNamePlaceholder;

        if (const auto description_ = result["description"].value<std::string>())
            this->description = description_->c_str();
        else
            this->description = std::string();

        if (const auto group_ = result["group"].value<std::string>())
            this->group = group_->c_str();
        else
            this->group = kDefaultGroup;

        LOG_DEBUGF("WorkflowMgr", "Parsed workflow file: %s", filePath.u8string().c_str());
        return true;
    }
    catch (const toml::parse_error& err)
    {
        LOG_ERRORF("WorkflowMgr", "Parse %s failed: %s", err.what());
        return false;
    }
}
