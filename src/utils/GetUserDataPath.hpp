#pragma once

/**
 * @file GetUserDataPath.hpp
 * @brief Resolves the platform-specific user-data directory for Manifold.
 *
 * Provides a single helper function, GetUserDataDirectory(), that returns the
 * conventional per-user application-data path on Windows, macOS, and Linux.
 * The returned path is not guaranteed to exist; callers are responsible for
 * creating it (e.g. via `std::filesystem::create_directories`) before writing.
 *
 * | Platform | Path                                              |
 * |----------|---------------------------------------------------|
 * | Windows  | `%APPDATA%\Manifold`                              |
 * | macOS    | `~/Library/Application Support/Manifold`          |
 * | Linux    | `~/.config/Manifold`                              |
 * | Fallback | `./` (when the relevant environment variable is unset) |
 */

#include <string>

/// The application name used as the final path component in user-data paths.
#define PROGRAM_NAME "Manifold"

/**
 * @brief Returns the platform-specific user-data directory for Manifold.
 *
 * Resolves the path by reading the appropriate environment variable for the
 * current platform (`APPDATA` on Windows, `HOME` on macOS/Linux) and appending
 * the application name.  If the environment variable is unset, the function
 * falls back to the current working directory (`".//"`).
 *
 * @note The returned path is not guaranteed to exist.  Create it with
 *       `std::filesystem::create_directories()` before writing files to it.
 *
 * @return Absolute path to the user-data directory, or `"./"` as a fallback.
 */
static std::string GetUserDataDirectory()
{
#if defined(_WIN32)         // Windows
    // Windows: %APPDATA%/Manifold
    const char* appData = std::getenv("APPDATA");

    if (appData) {
        std::string path(appData);
        path += "\\" + std::string(PROGRAM_NAME);
        return std::move(path);
    }
#elif defined(__APPLE__)    // macOS
    // macOS: ~/Library/Application Support/Cetone033
    const char* home = std::getenv("HOME");
    if (home) {
        std::string path(home);
        path += "/Library/Application Support/" + std::string(PROGRAM_NAME);
        return std::move(path);
    }
#else
    // Linux: ~/.config/Cetone033
    const char* home = std::getenv("HOME");
    if (home) {
        std::string path(home);
        path += "/.config/" + std::string(PROGRAM_NAME);
        return std::move(path);
    }
#endif
    return std::string(".//");
}
