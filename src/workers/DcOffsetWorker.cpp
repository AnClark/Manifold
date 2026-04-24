#include "DcOffsetWorker.hpp"

#include <cmath>

void DcOffsetWorker::processItem(std::shared_ptr<SndFileInfo> fileInfoInstance)
{
    if (!fileInfoInstance || fileInfoInstance->aboutToBeRemoved || shouldCancelProcessing)
        return;

    SF_INFO fileInfo = {};
    SNDFILE* file;

    fileInfoInstance->isDcOffsetCalculatedOK = false;
    fileInfoInstance->errorMsgDcOffset.clear();

    /* 1. 打开音频文件 */
#ifdef _WIN32
        // UTF-8 转 UTF-16 (Windows 宽字符)
        int wlen = MultiByteToWideChar(CP_UTF8, 0, fileInfoInstance->fileName.c_str(), -1, nullptr, 0);
        std::wstring wpath(wlen - 1, 0);
        MultiByteToWideChar(CP_UTF8, 0, fileInfoInstance->fileName.c_str(), -1, &wpath[0], wlen);
        
        file = sf_wchar_open(wpath.c_str(), SFM_READ, &fileInfo);
#else
        file = sf_open(fileInfoInstance->fileName.c_str(), SFM_READ, &fileInfo);
#endif
    if (!file) {
        fileInfoInstance->errorMsgDcOffset = "Cannot open file";
        fileInfoInstance->errorMsgDcOffset += fileInfoInstance->fileName;
        fileInfoInstance->errorMsgDcOffset += ": ";
        fileInfoInstance->errorMsgDcOffset += sf_strerror(NULL);
        return;
    }

    const int channels = fileInfo.channels;
    constexpr sf_count_t BLOCK = 4096;

    std::vector<float> buf(BLOCK * channels);
    fileInfoInstance->dcOffsets.assign(channels, 0.0);
    sf_count_t totalFrames = 0, read = 0;

    while ((read = sf_readf_float(file, buf.data(), BLOCK)) > 0) {
        for (sf_count_t f = 0; f < read; ++f)
            for (int c = 0; c < channels; ++c)
                fileInfoInstance->dcOffsets[c] += buf[f * channels + c];
        totalFrames += read;

        if (fileInfoInstance->aboutToBeRemoved || shouldCancelProcessing)
        {
            sf_close(file);
            return;
        }
    }
    sf_close(file);

    for (auto& s : fileInfoInstance->dcOffsets)
        s /= static_cast<double>(totalFrames);

    // Calculate max DC offset for display purposes
    double dcDisplay = 0.0;
    for (double v : fileInfoInstance->dcOffsets)
        dcDisplay = std::max(dcDisplay, std::abs(v));
    fileInfoInstance->maxDcOffset = dcDisplay;

    fileInfoInstance->isDcOffsetCalculatedOK = true;
}
