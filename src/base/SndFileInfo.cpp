#include "SndFileInfo.hpp"
#include "utils/SfOpenUtf8.hpp"

#include <filesystem>

void SndFileInfo::updateFilePath(const char* newFileName)
{
    this->filePath.assign(newFileName);
    this->fileNameBase = std::filesystem::path(newFileName).filename().string();
}

void SndFileInfo::parseSndFile()
{
    info = {};
    isParseOK = false;
    handle = nullptr;
    errorMsg.clear();

    handle = SfOpenUtf8(filePath, SFM_READ, &info);

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

void SndFileInfo::parseSndFile(const char* newFileName)
{
    updateFilePath(newFileName);
    parseSndFile();
}

double SndFileInfo::getDurationSeconds() const
{
    if (!isParseOK || info.samplerate <= 0)
        return 0.0;
    return static_cast<double>(info.frames) / info.samplerate;
}

const char* SndFileInfo::getBitDepth() const
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
        case SF_FORMAT_OPUS:
            return "Opus";
        case SF_FORMAT_PCM_U8:
            return "Unsigned 8-bit";
        case SF_FORMAT_MPEG_LAYER_I:
            return "MP1";
        case SF_FORMAT_MPEG_LAYER_II:
            return "MP2";
        case SF_FORMAT_MPEG_LAYER_III:
            return "MP3";
        
        // Fallback
        default:
            return "Unknown";
    }
}

bool SndFileInfo::isSpecialBitDepthFormat() const
{
    int formatSubType = info.format & SF_FORMAT_SUBMASK;
    switch (formatSubType)
    {
        case SF_FORMAT_VORBIS:
        case SF_FORMAT_OPUS:
        case SF_FORMAT_MPEG_LAYER_I:
        case SF_FORMAT_MPEG_LAYER_II:
        case SF_FORMAT_MPEG_LAYER_III:
            return true;
        default:
            return false;
    }
}

void SndFileInfo::_calculateDurationTimeString()
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
