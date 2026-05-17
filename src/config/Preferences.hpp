#pragma once

/**
 * @file Preferences.hpp
 * @brief Persistent Manifold application preferences backed by a TOML file.
 *
 * Preferences are split into typed sub-structs (e.g. @ref uiPref) that each
 * know how to serialise and deserialise themselves via `getTable()` /
 * `fromTable()`.  @ref PreferencesManager owns one instance of every sub-struct
 * and handles the file I/O: it loads on construction and saves on destruction,
 * so callers only need to keep a @ref PreferencesManager alive for the
 * lifetime of the application.
 *
 * ### File location
 * `<userDataDir>/preferences.toml` — resolved by GetUserDataDirectory().
 *
 * ### TOML schema
 * @code{.toml}
 * [ui]
 * action_left_panel_width_weight = 0.5
 * tasks_left_panel_weight        = 1.0
 * tasks_right_panel_weight       = 2.0
 * @endcode
 */

#include <toml.hpp>

#include "base/AudioFormats.hpp"
#include "config/FilenameConfig.hpp"
using namespace AudioFormats;

// --------------------------------------------------------------------------
// uiPref — UI layout preferences
// --------------------------------------------------------------------------

/**
 * @brief Persistent UI layout preferences.
 *
 * Stores the relative widths / weights of resizable panel splitters so that
 * the layout is preserved across sessions.
 *
 * Each field maps to a TOML key under the `[ui]` section (see
 * @ref getTable() / @ref fromTable() for the exact key names).
 */
struct uiPref
{
    /// Relative width weight of the left panel in the Actions view splitter.
    float actionLeftPanelWeight = 0.5f;

    float actionFooterPaneHeight    = 220.0f;
    bool  actionFooterPaneCollapsed = false;

    /// Relative width weight of the left panel in the Tasks view splitter.
    float tasksLeftPanelWeight = 1.0f;

    /// Relative width weight of the right panel in the Tasks view splitter.
    float tasksRightPanelWeight = 2.0f;

    /// Absolute widths of Tasks view's Details table columns.
    float tasksFilesColumnWidths[4] = { 300.0f, 60.0f, 180.0f, 160.0f };

    /**
     * @brief Serialises all fields into a `toml::table`.
     *
     * The returned table is intended to be embedded under the `[ui]` key of
     * the root preferences document by @ref PreferencesManager::savePreferences().
     *
     * @return A `toml::table` containing one `double` entry per field.
     */
    toml::table getTable() const
    {
        toml::table table;
        table.insert_or_assign("action_left_panel_width_weight",
                               static_cast<double>(actionLeftPanelWeight));
        table.insert_or_assign("action_footer_pane_height",
                               static_cast<double>(actionFooterPaneHeight));
        table.insert_or_assign("action_footer_pane_collapsed",
                               static_cast<bool>(actionFooterPaneCollapsed));                               
        table.insert_or_assign("tasks_left_panel_weight",
                               static_cast<double>(tasksLeftPanelWeight));
        table.insert_or_assign("tasks_right_panel_weight",
                               static_cast<double>(tasksRightPanelWeight));
        toml::array tasksFilesColWidths;
        for (int i = 0; i < 4; ++i)
            tasksFilesColWidths.push_back(static_cast<double>(tasksFilesColumnWidths[i]));
        table.insert_or_assign("tasks_files_column_width", std::move(tasksFilesColWidths));
        return table;
    }

    /**
     * @brief Populates fields from a `toml::table` read from disk.
     *
     * Each recognised key is read and converted to `float`.  Unknown keys are
     * silently ignored; missing keys retain their default values, ensuring
     * forward compatibility when new fields are added in future versions.
     *
     * @param table The `[ui]` sub-table from the parsed preferences document.
     */
    void fromTable(const toml::table& table)
    {
        if (auto v = table["action_left_panel_width_weight"].value<double>())
            actionLeftPanelWeight = static_cast<float>(*v);
        if (auto v = table["action_footer_pane_height"].value<double>())
            actionFooterPaneHeight = static_cast<float>(*v);
        if (auto v = table["action_footer_pane_collapsed"].value<bool>())
            actionFooterPaneCollapsed = static_cast<bool>(*v);        
        if (auto v = table["tasks_left_panel_weight"].value<double>())
            tasksLeftPanelWeight = static_cast<float>(*v);
        if (auto v = table["tasks_right_panel_weight"].value<double>())
            tasksRightPanelWeight = static_cast<float>(*v);
        if (auto* arr = table["tasks_files_column_width"].as_array())
            for (int i = 0; i < 4 && i < static_cast<int>(arr->size()); ++i)
                if (auto v = arr->get(i)->value<double>())
                    tasksFilesColumnWidths[i] = static_cast<float>(*v);
    }
};

struct OutputConfigPref
{
    std::string     outputPath;
    ContainerFormat outputFormat  = ContainerFormat::Wav;
    SubtypeOverride outputSubtype = SubtypeOverride::Auto;
    bool            nullOutput    = false;

    FilenameTemplate filenameTemplate = FilenameTemplate::makeDefault();

    toml::table getTable() const
    {
        toml::table table;
        table.insert_or_assign("path", outputPath);
        table.insert_or_assign("format", outputFormat);
        table.insert_or_assign("subtype", outputSubtype);
        table.insert_or_assign("enable_null_output", static_cast<int>(nullOutput));
        table.insert_or_assign("filename", filenameTemplate.toToml());
        return table;
    }

    void fromTable(const toml::table& table)
    {
        if (auto v = table["path"].value<std::string>())
            outputPath = std::move(*v);
        if (auto v = table["format"].value<int64_t>())
            outputFormat = static_cast<ContainerFormat>(*v);
        if (auto v = table["subtype"].value<int64_t>())
            outputSubtype = static_cast<SubtypeOverride>(*v);
        if (auto v = table["enable_null_output"].value<bool>())
            nullOutput = static_cast<bool>(*v);
        if (const auto* fnTable = table["filename"].as_table())
            filenameTemplate = FilenameTemplate::fromToml(*fnTable);
    }
};

struct ActionUIPref
{
    bool exportNodeChainWithOutputConfig = true;
    bool importNodeChainWithOutputConfig = true;
    bool rememberRecentOutputConfigPref = true;

    toml::table getTable() const
    {
        toml::table table;
        table.insert_or_assign("export_node_chain_with_output_config", exportNodeChainWithOutputConfig);
        table.insert_or_assign("import_node_chain_with_output_config", importNodeChainWithOutputConfig);
        table.insert_or_assign("remember_recent_output_config_pref", rememberRecentOutputConfigPref);
        return table;
    }

    void fromTable(const toml::table& table)
    {
        if (auto v = table["export_node_chain_with_output_config"].value<bool>())
            exportNodeChainWithOutputConfig = static_cast<bool>(*v);
        if (auto v = table["import_node_chain_with_output_config"].value<bool>())
            importNodeChainWithOutputConfig = static_cast<bool>(*v);
        if (auto v = table["remember_recent_output_config_pref"].value<bool>())
            rememberRecentOutputConfigPref = static_cast<bool>(*v);
    }
};

// --------------------------------------------------------------------------
// PreferencesManager
// --------------------------------------------------------------------------

/**
 * @brief RAII owner for all application preferences.
 *
 * Automatically loads preferences from disk on construction and saves them on
 * destruction.  Callers should keep a single instance alive for the duration
 * of the application (e.g. as a member of the top-level application class).
 *
 * If the preferences file does not exist (first run) or is malformed, all
 * sub-structs retain their default values and no error is raised.
 */
class PreferencesManager
{
public:
    /**
     * @brief Constructs the manager and immediately loads preferences from disk.
     *
     * If the file is absent or cannot be parsed, defaults are kept silently.
     */
    PreferencesManager()
    {
        // Automatically load preferences on construction.
        loadPreferences();
    }

    /**
     * @brief Destroys the manager and immediately persists preferences to disk.
     *
     * Creates the user-data directory if it does not yet exist.
     */
    ~PreferencesManager()
    {
        // Automatically persist preferences on destruction.
        savePreferences();
    }

    /**
     * @brief Reads preferences from `<userDataDir>/preferences.toml`.
     *
     * Silently keeps all defaults if the file does not exist or cannot be
     * parsed.
     */
    void loadPreferences();

    /// Set to true by loadPreferences() when the saved filename template was
    /// invalid and has been silently reset to the built-in default.
    /// The UI should check this on the first rendered frame, show a warning
    /// notification, then clear the flag.
    bool filenameTemplateWasResetOnLoad = false;

    /**
     * @brief Writes current preferences to `<userDataDir>/preferences.toml`.
     *
     * Creates the user-data directory (and any intermediate directories) if
     * they do not yet exist.
     */
    void savePreferences();

    /// UI layout preferences (panel splitter weights).
    uiPref uiPref;

    /// Output config preferences
    OutputConfigPref outputConfigPref;

    /// Action UI preferences
    ActionUIPref actionUIPref;
};
