#include "DcOffsetWorker.hpp"

#include "utils/SfOpenUtf8.hpp"

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
    file = SfOpenUtf8(fileInfoInstance->filePath, SFM_READ, &fileInfo);
    if (!file) {
        fileInfoInstance->errorMsgDcOffset = "Cannot open file";
        fileInfoInstance->errorMsgDcOffset += fileInfoInstance->filePath;
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

    if (totalFrames == 0) {
        // Empty file — leave all offsets at 0 (no bias to remove)
        fileInfoInstance->isDcOffsetCalculatedOK = true;
        return;
    }

    for (auto& s : fileInfoInstance->dcOffsets)
        s /= static_cast<double>(totalFrames);

    // Calculate max DC offset for display purposes
    double dcDisplay = 0.0;
    for (double v : fileInfoInstance->dcOffsets)
        dcDisplay = std::max(dcDisplay, std::abs(v));
    fileInfoInstance->maxDcOffset = dcDisplay;

    fileInfoInstance->isDcOffsetCalculatedOK = true;
}
