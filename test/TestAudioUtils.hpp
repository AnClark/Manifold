#pragma once

// TestAudioUtils.hpp — shared audio-fixture generation for the test suite.
//
// The test binaries synthesise their audio fixtures with libsndfile instead of
// relying on externally supplied input files, so they run stand-alone under
// ctest as well as manually from the command line. This header is the single
// place where test audio material is created — keep it that way.

#include "utils/SfOpenUtf8.hpp"

#include <sndfile.h>

#if _WIN32
#include <shellapi.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace TestAudioUtils {

namespace detail { constexpr double kPi = 3.14159265358979323846; }

/**
 * @brief Create a stereo 16-bit PCM WAV file and return the open write handle.
 *
 * The caller owns the handle and must sf_close() it. Uses SfOpenUtf8 so the
 * tests also work with non-ASCII scratch paths on Windows (same convention as
 * the production code).
 *
 * @param path       Output path (parent directories must exist).
 * @param sampleRate Sample rate in Hz.
 */
inline SNDFILE* createWav(const std::string& path, int sampleRate)
{
    SF_INFO info = {};
    info.samplerate = sampleRate;
    info.channels   = 2;
    info.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE* sf = SfOpenUtf8(path, SFM_WRITE, &info);
    if (!sf)
        throw std::runtime_error("TestAudioUtils: cannot create " + path);
    return sf;
}

/**
 * @brief Write a stereo sine WAV (a different frequency per channel).
 *
 * @param path       Output path (parent directories must exist).
 * @param seconds    Duration in seconds.
 * @param sampleRate Sample rate in Hz.
 * @param freqL      Left-channel frequency in Hz.
 * @param freqR      Right-channel frequency in Hz.
 */
inline void writeSineWav(const std::string& path, double seconds = 0.5,
                         int sampleRate = 44100,
                         double freqL = 440.0, double freqR = 220.0)
{
    SNDFILE* sf = createWav(path, sampleRate);

    const int totalFrames = static_cast<int>(seconds * sampleRate);
    std::vector<float> buf(4096 * 2);
    int written = 0;
    while (written < totalFrames)
    {
        const int block = std::min(4096, totalFrames - written);
        for (int f = 0; f < block; ++f)
        {
            const double t = static_cast<double>(written + f) / sampleRate;
            buf[static_cast<size_t>(f) * 2 + 0] =
                0.5f * static_cast<float>(std::sin(2.0 * detail::kPi * freqL * t));
            buf[static_cast<size_t>(f) * 2 + 1] =
                0.5f * static_cast<float>(std::sin(2.0 * detail::kPi * freqR * t));
        }
        sf_writef_float(sf, buf.data(), block);
        written += block;
    }
    sf_close(sf);
}

/** @brief Write a valid header-only WAV with zero frames (edge-case fixture). */
inline void writeEmptyWav(const std::string& path, int sampleRate = 44100)
{
    SNDFILE* sf = createWav(path, sampleRate);
    sf_close(sf);
}

/**
 * @brief Create a clean scratch directory at the given path.
 *
 * Any previous content is removed first, so repeated runs start fresh.
 * Throws if the path exists and is not a directory, so a misplaced file
 * argument is never deleted.
 */
inline std::filesystem::path prepareScratchDir(const std::filesystem::path& dir)
{
    std::error_code ec;
    if (std::filesystem::exists(dir, ec) && !std::filesystem::is_directory(dir, ec))
        throw std::runtime_error("TestAudioUtils: scratch path is not a directory: " + dir.string());

    std::filesystem::remove_all(dir, ec);
    std::filesystem::create_directories(dir);
    return dir;
}

/**
 * @brief Resolve the scratch directory from the command line.
 *
 * Usage: `<test_binary> [scratch_dir]` — every test-suite binary takes the same
 * single optional argument: the directory in which it generates its fixtures
 * and outputs.
 *
 * @param argc, argv   main() arguments; argv[1] (optional) is the scratch dir.
 * @param fallbackName Directory name under the system temp directory, used
 *                     when no argument is given (bare manual runs).
 *
 * @note The project-level default (PROJECT_BINARY_DIR/tmp) is intentionally
 *       NOT hardcoded here — it is passed explicitly by add_test() in
 *       CMakeLists.txt.
 *
 * @note On Windows the arguments are re-read from GetCommandLineW(): argv[]
 *       uses the ANSI code page there, which breaks UTF-8 paths (e.g. non-
 *       ASCII user names in the temp path). This keeps main() portable across
 *       console and GUI subsystems.
 */
inline std::filesystem::path scratchDirFromArgs(int argc, char** argv,
                                               const std::string& fallbackName)
{
    std::filesystem::path dir;
#ifdef _WIN32
    (void)argc; (void)argv;
    int wargc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &wargc);
    if (wargv)
    {
        if (wargc >= 2)
            dir = std::filesystem::path(wargv[1]);
        LocalFree(wargv);
    }
#else
    if (argc >= 2)
        dir = std::filesystem::u8path(argv[1]);
#endif

    if (dir.empty())
        dir = std::filesystem::temp_directory_path() / fallbackName;

    try
    {
        return prepareScratchDir(dir);
    }
    catch (const std::exception& e)
    {
        // Fail cleanly (message + exit code 1) instead of terminating on an
        // uncaught exception — ctest treats both as a failed test either way.
        std::fprintf(stderr, "%s\n", e.what());
        std::exit(1);
    }
}

} // namespace TestAudioUtils