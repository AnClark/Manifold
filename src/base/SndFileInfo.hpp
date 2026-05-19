#pragma once

#include <sndfile.h>

#include <atomic>
#include <memory>
#include <string>
#include <vector>
#include <queue>

struct SndFileInfo
{
    std::string filePath;       // Full filename with path
    std::string fileNameBase;   // Base filename
    SNDFILE *handle = nullptr;  // Handle for parsing sound file, not kept open after parsing is done

    SF_INFO info = {};
    double lufsI = 0.0f;
    double maxSamplePeak = 0.0f;
    double maxSamplePeak_dBFS = 0.0f;
    std::vector<double> dcOffsets;  // Per-channel DC offset values
    double maxDcOffset = 0.0;  // Maximum absolute DC offset across all channels

    bool isParseOK = false;
    std::string errorMsg;

    bool isR128ParsedOK = false;
    std::string errorMsgR128;

    bool isDcOffsetCalculatedOK = false;
    std::string errorMsgDcOffset;

    bool selected = false;           // Mark if selected in File List
    std::atomic<bool> aboutToBeRemoved{false}; // Set to true when removed by user; workers skip processing
    
    char durationString[16] = "---"; // Cache for duration string to avoid repeated formatting

    void updateFilePath(const char* newFileName);
    void parseSndFile();
    void parseSndFile(const char* newFileName);
    double getDurationSeconds() const;
    const char* getBitDepth() const;
    bool isSpecialBitDepthFormat() const;

private:
    void _calculateDurationTimeString();
};

typedef std::vector<std::shared_ptr<SndFileInfo>> SndFileList;
typedef std::queue<std::shared_ptr<SndFileInfo>> PendingSndFileQueue;
