#pragma once
#include <sndfile.h>
#include <string>

#if _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

/************************************************************
 * SfOpenUtf8.hpp
 *
 * A simple wrapper around libsndfile's sf_open function to handle
 * UTF-8 file paths across platforms, especially Windows.
 *
 * Provides a single function:
 *   SNDFILE* sfOpenUtf8(const std::string& path, int mode, SF_INFO* info);
 *
 * This function will attempt to open the specified audio file using
 * the provided mode and info struct. On Windows, it converts the UTF-8
 * path to a wide-character string before calling sf_wchar_open. On other
 * platforms, it calls sf_open directly with the UTF-8 path.
 *
 * Usage:
 *   SF_INFO info = {};
 *   SNDFILE* file = sfOpenUtf8("path/to/audio.wav", SFM_READ, &info);
 *
 * Note: The caller is responsible for closing the returned SNDFILE* handle
 *       using sf_close when done.
 ************************************************************/

/**
 * @brief Opens an audio file with a UTF-8 encoded path, handling platform-specific differences.
 *
 * @param path The UTF-8 encoded file path to open.
 * @param mode The mode to open the file (e.g., SFM_READ, SFM_WRITE).
 * @param info Pointer to an SF_INFO struct that will be filled with file information.
 *
 * @return A pointer to an SNDFILE handle if successful, or nullptr on failure.
 */
static SNDFILE* SfOpenUtf8(const std::string& path, int mode, SF_INFO* info)
{
#ifdef _WIN32
    // UTF-8 转 UTF-16 (Windows 宽字符)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlen - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, &wpath[0], wlen);
    
    return sf_wchar_open(wpath.c_str(), mode, info);
#else
    return sf_open(path.c_str(), mode, info);
#endif
}
