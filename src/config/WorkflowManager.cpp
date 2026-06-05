#include "WorkflowManager.hpp"
#include "utils/GetUserDataPath.hpp"
#include "utils/LogManager.hpp"
#include "Config.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <unordered_set>

// Returns the full path to the workflow folder.
static constexpr const char* kWorkflowDirName = "workflows";

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

            // Derive group from the immediate parent directory name.
            // Files in workflows/[group]/ get that directory as their group.
            // Files placed directly under workflows/ (no sub-directory) are
            // ungrouped — represented by an empty string.
            const auto parentName = entry.path().parent_path().filename().u8string();
            wd.group = (parentName == kWorkflowDirName) ? "" : parentName;
            if (wd.parseMetadata())
            {
                // Check for a duplicate name within the same group.
                // This can happen when the user manually copies files on disk.
                // Resolve it by renaming the incoming file with a timestamp suffix.
                const bool isDuplicate = std::any_of(target.begin(), target.end(),
                    [&](const WorkflowDescriptor& e)
                    { return e.name == wd.name && e.group == wd.group; });

                // De-duplicate strategy: automatically append non-unique workflow name with unique suffix.
                //     Only the `name` field needs to be unique — the filename is irrelevant
                //     to the user and does not need to change.
                if (isDuplicate)
                {
                    // Generate a time suffix of current time.
                    char timeBuf[16];
                    auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
                    std::strftime(timeBuf, sizeof(timeBuf), "_%H%M%S", std::localtime(&t));

                    // Find a name that is not already taken in target (same-second duplicates
                    // get _HHmmss_1, _HHmmss_2, ... to stay distinct).
                    std::string uniqueSuffix = timeBuf;
                    std::string newName      = wd.name + uniqueSuffix;

                    for (int counter = 1;
                         std::any_of(target.begin(), target.end(),
                             [&](const WorkflowDescriptor& e)
                             { return e.name == newName && e.group == wd.group; });
                         ++counter)
                    {
                        uniqueSuffix = std::string(timeBuf) + "_" + std::to_string(counter);
                        newName      = wd.name + uniqueSuffix;
                    }

                    // Rewrite only the `name` field inside the TOML file; filename unchanged.
                    bool rewriteOk = false;
                    try
                    {
                        std::ifstream ifs(wd.filePath);
                        if (ifs.is_open())
                        {
                            auto doc = toml::parse(ifs, wd.filePath.u8string());
                            doc.insert_or_assign("name", newName);
                            std::ofstream ofs(wd.filePath, std::ios::out | std::ios::trunc);
                            if (ofs.is_open())
                            {
                                ofs << doc;
                                rewriteOk = !ofs.fail();
                            }
                        }
                    }
                    catch (...) {}

                    const std::string originalName = wd.name;
                    if (rewriteOk)
                        wd.name = newName;

                    const std::string msg =
                        "Duplicate workflow name \"" + originalName + "\"" +
                        (wd.group.empty() ? std::string{} : " in group \"" + wd.group + "\"") +
                        (rewriteOk
                            ? " — renamed to \"" + newName + "\" in " + wd.filePath.filename().u8string()
                            : " — could not rename (file write failed): " + wd.filePath.filename().u8string());
                    LOG_WARNF("WorkflowMgr", "%s", msg.c_str());
                    _scanNotifications.push_back({msg});
                }
                target.push_back(std::move(wd));
            }
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
        if (w.group.empty())  // ungrouped items are not a "group"
            continue;
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

        LOG_DEBUGF("WorkflowMgr", "Parsed workflow file: %s", filePath.u8string().c_str());
        return true;
    }
    catch (const toml::parse_error& err)
    {
        LOG_ERRORF("WorkflowMgr", "Parse %s failed: %s", err.what());
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// File-name sanitization
// ─────────────────────────────────────────────────────────────────────────────

std::string WorkflowManager::_sanitizeForFilename(const std::string& str)
{
    // Characters illegal in filenames on Windows (and some on POSIX).
    // Non-ASCII bytes (UTF-8 multibyte sequences for CJK etc.) are kept intact.
    static constexpr std::string_view kForbidden = R"(\/:*?"<>|)";

    std::string result;
    result.reserve(str.size());
    for (unsigned char c : str)
    {
        if (c < 32)   // strip control characters
            continue;
        result += (kForbidden.find(static_cast<char>(c)) != std::string_view::npos)
                  ? '_' : static_cast<char>(c);
    }

    // Trim leading/trailing spaces and dots (Windows restriction on those suffixes)
    const auto first = result.find_first_not_of(" .");
    if (first == std::string::npos)
        return {};
    const auto last = result.find_last_not_of(" .");
    result = result.substr(first, last - first + 1);

    // Truncate to a sane byte length — 64 bytes keeps short filenames readable
    if (result.size() > 64)
        result.resize(64);

    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Save
// ─────────────────────────────────────────────────────────────────────────────

bool WorkflowManager::saveWorkflow(const std::string& name,
                                   const std::string& description,
                                   const std::string& group,
                                   const std::string& tomlContent)
{
    // 1. Target directory: workflows/[group_sanitized]/ or workflows/ when ungrouped.
    const std::string groupStem = group.empty() ? "" : _sanitizeForFilename(group);
    const std::filesystem::path targetDir = groupStem.empty()
        ? preferencesFilePath()
        : preferencesFilePath() / groupStem;

    std::error_code ec;
    std::filesystem::create_directories(targetDir, ec);
    if (ec)
    {
        LOG_ERRORF("WorkflowMgr", "Failed to create workflow directory '%s': %s",
                   targetDir.u8string().c_str(), ec.message().c_str());
        return false;
    }

    // 2. Resolve target path.
    //    If a workflow with the same name+group already exists, overwrite that file.
    //    Otherwise derive a filename from the name, handling the rare edge case where
    //    two different display-names sanitize to the same stem.
    std::filesystem::path targetPath;
    bool overwriting = false;
    for (const auto& w : workflows)
    {
        if (w.name == name && w.group == group)
        {
            targetPath  = w.filePath;
            overwriting = true;
            break;
        }
    }
    if (!overwriting)
    {
        std::string stem = _sanitizeForFilename(name);
        if (stem.empty())
        {
            auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            char buf[32];
            std::strftime(buf, sizeof(buf), "workflow_%Y%m%d_%H%M%S", std::localtime(&t));
            stem = buf;
        }
        targetPath = targetDir / (stem + ".toml");
        // Edge case: different display-name but same sanitized stem → add timestamp
        if (std::filesystem::exists(targetPath))
        {
            auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
            char timeBuf[16];
            std::strftime(timeBuf, sizeof(timeBuf), "_%H%M%S", std::localtime(&t));
            targetPath = targetDir / (stem + timeBuf + ".toml");
        }
    }

    // 4. Write — ofstream(filesystem::path) uses the wide-char API on Windows,
    //    correctly handling non-ASCII (CJK) paths.
    std::ofstream ofs(targetPath, std::ios::out | std::ios::trunc);
    if (!ofs.is_open())
    {
        LOG_ERRORF("WorkflowMgr", "Failed to open workflow file for writing: %s",
                   targetPath.u8string().c_str());
        return false;
    }
    ofs << tomlContent;
    if (ofs.fail())
    {
        LOG_ERRORF("WorkflowMgr", "Write error for workflow file: %s",
                   targetPath.u8string().c_str());
        return false;
    }
    ofs.close();

    LOG_INFOF("WorkflowMgr", "%s workflow '%s' -> %s",
              overwriting ? "Overwrote" : "Saved",
              name.c_str(), targetPath.u8string().c_str());

    // 5. Refresh list
    scanUserWorkflow(true);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Delete
// ─────────────────────────────────────────────────────────────────────────────

bool WorkflowManager::nameExistsInGroup(const std::string& name, const std::string& group) const
{
    for (const auto& w : workflows)
        if (w.name == name && w.group == group)
            return true;
    return false;
}

std::vector<ScanNotification> WorkflowManager::drainScanNotifications()
{
    std::vector<ScanNotification> result;
    result.swap(_scanNotifications);
    return result;
}

bool WorkflowManager::deleteWorkflow(const WorkflowDescriptor& w)
{
    std::error_code ec;
    std::filesystem::remove(w.filePath, ec);
    if (ec)
    {
        LOG_ERRORF("WorkflowMgr", "Failed to delete workflow file '%s': %s",
                   w.filePath.u8string().c_str(), ec.message().c_str());
        return false;
    }
    LOG_INFOF("WorkflowMgr", "Deleted workflow file: %s", w.filePath.u8string().c_str());
    scanUserWorkflow(true);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Rename
// ─────────────────────────────────────────────────────────────────────────────

bool WorkflowManager::renameWorkflow(const WorkflowDescriptor& w, const std::string& newName)
{
    if (newName.empty())
        return false;

    // Reject if the new name is already taken in the same group (excluding self)
    for (const auto& existing : workflows)
    {
        if (&existing == &w) continue;
        if (existing.name == newName && existing.group == w.group)
        {
            LOG_ERRORF("WorkflowMgr", "Rename failed: name \"%s\" already exists in group \"%s\"",
                       newName.c_str(), w.group.c_str());
            return false;
        }
    }

    // Rewrite the `name` field inside the TOML file; filename stays unchanged.
    try
    {
        std::ifstream ifs(w.filePath);
        if (!ifs.is_open())
        {
            LOG_ERRORF("WorkflowMgr", "Rename failed: cannot open \"%s\" for reading",
                       w.filePath.u8string().c_str());
            return false;
        }
        auto doc = toml::parse(ifs, w.filePath.u8string());
        doc.insert_or_assign("name", newName);

        std::ofstream ofs(w.filePath, std::ios::out | std::ios::trunc);
        if (!ofs.is_open() || !(ofs << doc) || ofs.fail())
        {
            LOG_ERRORF("WorkflowMgr", "Rename failed: cannot write \"%s\"",
                       w.filePath.u8string().c_str());
            return false;
        }
    }
    catch (const toml::parse_error& err)
    {
        LOG_ERRORF("WorkflowMgr", "Rename failed (TOML parse): %s", err.what());
        return false;
    }

    LOG_INFOF("WorkflowMgr", "Renamed workflow \"%s\" -> \"%s\" in %s",
              w.name.c_str(), newName.c_str(), w.filePath.u8string().c_str());
    scanUserWorkflow(true);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// Move to group
// ─────────────────────────────────────────────────────────────────────────────

bool WorkflowManager::moveWorkflowToGroup(const WorkflowDescriptor& w, const std::string& newGroup)
{
    // Same group → no-op
    if (w.group == newGroup)
        return true;

    // Target directory
    const std::string groupStem = newGroup.empty() ? "" : _sanitizeForFilename(newGroup);
    const std::filesystem::path targetDir = groupStem.empty()
        ? preferencesFilePath()
        : preferencesFilePath() / groupStem;

    std::error_code ec;
    std::filesystem::create_directories(targetDir, ec);
    if (ec)
    {
        LOG_ERRORF("WorkflowMgr", "moveWorkflowToGroup: cannot create dir '%s': %s",
                   targetDir.u8string().c_str(), ec.message().c_str());
        return false;
    }

    // Resolve a non-colliding destination path
    std::filesystem::path destPath = targetDir / w.filePath.filename();
    if (std::filesystem::exists(destPath))
    {
        auto t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        char timeBuf[16];
        std::strftime(timeBuf, sizeof(timeBuf), "_%H%M%S", std::localtime(&t));
        destPath = targetDir / (w.filePath.stem().string() + timeBuf + ".toml");
    }

    std::filesystem::rename(w.filePath, destPath, ec);
    if (ec)
    {
        LOG_ERRORF("WorkflowMgr", "moveWorkflowToGroup: rename failed '%s' -> '%s': %s",
                   w.filePath.u8string().c_str(), destPath.u8string().c_str(), ec.message().c_str());
        return false;
    }

    LOG_INFOF("WorkflowMgr", "Moved workflow \"%s\" to group \"%s\" (%s)",
              w.name.c_str(), newGroup.c_str(), destPath.u8string().c_str());
    scanUserWorkflow(true);
    return true;
}
