#pragma once

#include <sndfile.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <queue>

#if _WIN32
#define WIN32_MEAN_AND_LEAN
#include <windows.h>
#endif

struct SndFileInfo
{
    std::string fileName;
    SNDFILE *handle = nullptr;  // Handle for parsing sound file, not kept open after parsing is done

    SF_INFO info = {};
    double lufsI = 0.0f;
    double maxTruePeak = 0.0f;
    double maxTruePeak_dBTP = 0.0f;

    bool isParseOK = false;
    std::string errorMsg;

    bool isR128ParsedOK = false;
    std::string errorMsgR128;

    bool selected = false;           // Mark if selected in File List
    std::atomic<bool> aboutToBeRemoved{false}; // Set to true when removed by user; workers skip processing
    
    char durationString[16] = "---"; // Cache for duration string to avoid repeated formatting

    void parseSndFile()
    {
        info = {};
        isParseOK = false;
        handle = nullptr;
        errorMsg.clear();

#ifdef _WIN32
        // UTF-8 转 UTF-16 (Windows 宽字符)
        int wlen = MultiByteToWideChar(CP_UTF8, 0, fileName.c_str(), -1, nullptr, 0);
        std::wstring wpath(wlen - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, fileName.c_str(), -1, &wpath[0], wlen);
        
        handle = sf_wchar_open(wpath.c_str(), SFM_READ, &info);
#else
        handle = sf_open(fileName.c_str(), SFM_READ, &info);
#endif

        if (handle == nullptr)
        {
            errorMsg = sf_error_number(sf_error(nullptr));
            isParseOK = false;

            return;
        }

        sf_close(handle);
        handle = nullptr;
        isParseOK = true;

        _calculateDurationTimeString();        
    }

    void parseSndFile(const char* newFileName)
    {
        this->fileName = newFileName;
        parseSndFile();
    }

    double getDurationSeconds() const
    {
        if (!isParseOK || info.samplerate <= 0)
            return 0.0;
        return static_cast<double>(info.frames) / info.samplerate;
    }

    const char* getBitDepth() const
    {
        int formatSubType = info.format & SF_FORMAT_SUBMASK;
        switch (formatSubType)
        {
            // Readable bit depths
            case SF_FORMAT_PCM_S8:
                return "8-bit";
            case SF_FORMAT_PCM_16:
            case SF_FORMAT_ALAC_16:
                return "16-bit";
            case SF_FORMAT_ALAC_20:
                return "20-bit";
            case SF_FORMAT_PCM_24:
            case SF_FORMAT_ALAC_24:
                return "24-bit";
            case SF_FORMAT_PCM_32:
            case SF_FORMAT_ALAC_32:
                return "32-bit";
            case SF_FORMAT_FLOAT:
                return "32-bit float";
            case SF_FORMAT_DOUBLE:
                return "64-bit float";
            
            // Some special formats with non-standard bit depths
            case SF_FORMAT_VORBIS:
                return "Vorbis";
            case SF_FORMAT_PCM_U8:
                return "Unsigned 8-bit";
            
            // Fallback
            default:
                return "Unknown";
        }
    }

private:
    void _calculateDurationTimeString()
    {
        double duration = getDurationSeconds();
        if (duration <= 0.0)
        {
            snprintf(durationString, sizeof(durationString), "---");
            return;
        }

        int totalMs = static_cast<int>(duration * 1000.0 + 0.5);
        int minutes = totalMs / 60000;
        int seconds = (totalMs % 60000) / 1000;
        int ms      = totalMs % 1000;

        snprintf(durationString, sizeof(durationString), "%d:%02d.%03d", minutes, seconds, ms);
    }
};

typedef std::vector<std::shared_ptr<SndFileInfo>> SndFileList;
typedef std::queue<std::shared_ptr<SndFileInfo>> PendingSndFileQueue;
