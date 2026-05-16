#include "Preferences.hpp"
#include "utils/GetUserDataPath.hpp"
#include "utils/LogManager.hpp"

#include <filesystem>
#include <fstream>

static constexpr const char* kPreferencesFileName = "preferences.toml";

// Returns the full path to the preferences file.
static std::filesystem::path preferencesFilePath()
{
    return std::filesystem::path(GetUserDataDirectory()) / kPreferencesFileName;
}

void PreferencesManager::loadPreferences()
{
    const std::filesystem::path prefPath = preferencesFilePath();

    // No file yet — keep all defaults silently.
    if (!std::filesystem::exists(prefPath))
    {
        LOG_DEBUGF("Config", "Preference file not created yet. Will load default preferences.");
        return;
    }

    // Malformed file — keep all defaults silently.
    // TODO: surface a warning through LogManager once it is accessible here.
    try
    {
        // Open via ifstream(filesystem::path): on Windows this calls the wide-char
        // (_wfopen) API internally, correctly handling UTF-8 / CJK paths.
        // toml::parse_file(string) uses fopen(char*) which treats the path as
        // ANSI on Windows, silently failing for non-ASCII paths.
        std::ifstream ifs(prefPath, std::ios::in);
        if (!ifs.is_open())
        {
            LOG_ERRORF("Config", "Cannot load preference file. Will fallback to default preferences.");
            return; // File unreadable — keep all defaults silently.
        }

        auto result = toml::parse(ifs, prefPath.u8string());

        if (const auto* ui = result["ui"].as_table())
            uiPref.fromTable(*ui);
        if (const auto* outputConfig = result["output_config"].as_table())
            outputConfigPref.fromTable(*outputConfig);
        if (const auto* actionUI = result["action_ui"].as_table())
            actionUIPref.fromTable(*actionUI);

        LOG_DEBUGF("Config", "Loaded preferences from config file: %s", prefPath.u8string().c_str());
    }
    catch (const toml::parse_error& err)
    {
        LOG_ERRORF("Config", "Failed to load config file: %s", err.what());
    }
}

void PreferencesManager::savePreferences()
{
    const std::filesystem::path prefPath = preferencesFilePath();

    // Ensure the user-data directory exists.
    std::error_code ec;
    std::filesystem::create_directories(prefPath.parent_path(), ec);
    // If create_directories fails, the ofstream open will also fail — handled below.

    toml::table root;
    root.insert_or_assign("ui", uiPref.getTable());
    root.insert_or_assign("output_config", outputConfigPref.getTable());
    root.insert_or_assign("action_ui", actionUIPref.getTable());

    std::ofstream file(prefPath);
    if (file.is_open())
        file << root;
}
