#include "EBUR128Worker.hpp"

#include "ebur128.h"

#include <cmath>

void EBUR128Worker::ebur128WorkerMainFuction(EBUR128Worker *workerInstance)
{
    while (true)
    {
        std::shared_ptr<SndFileInfo> currentFile;

        {
            std::unique_lock<std::mutex> lock(workerInstance->pendingFileListMutex);

            workerInstance->cv.wait(lock, [&] {
                return workerInstance->shouldExit || !workerInstance->pendingFileList.empty();
            });

            if (workerInstance->shouldExit)
                return;

            currentFile = workerInstance->pendingFileList.front();
            workerInstance->pendingFileList.pop();
        }

        if (currentFile && !currentFile->cancelled)
            workerInstance->processFile(currentFile);
    }
}

void EBUR128Worker::processFile(std::shared_ptr<SndFileInfo> fileInfoInstance)
{
    if (!fileInfoInstance || fileInfoInstance->cancelled)
        return;

    SF_INFO fileInfo = {};
    SNDFILE* file;
    ebur128_state* st = nullptr;
    double* buffer = nullptr;
    sf_count_t framesRead;

    fileInfoInstance->isR128ParsedOK = false;
    fileInfoInstance->errorMsgR128.clear();

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
        fileInfoInstance->errorMsgR128 = "Cannot open file";
        fileInfoInstance->errorMsgR128 += fileInfoInstance->fileName;
        fileInfoInstance->errorMsgR128 += ": ";
        fileInfoInstance->errorMsgR128 += sf_strerror(NULL);
        return;
    }

    /* 2. 初始化 ebur128
     *    EBUR128_MODE_I: 启用 LUFS-I (Integrated Loudness) 测量
     *    EBUR128_MODE_TRUE_PEAK: 启用 True Peak 测量
     */
    st = ebur128_init(
        (unsigned)fileInfo.channels,
        (unsigned)fileInfo.samplerate,
        EBUR128_MODE_I | EBUR128_MODE_TRUE_PEAK
    );
    if (!st) {
        fileInfoInstance->errorMsgR128 = "Cannot initialize ebur128_state";
        sf_close(file);
        return;
    }

    /* 3. 配置声道映射（可选）
     *    对于常见配置，库会使用合理的默认值
     *    这里仅作为示例展示如何自定义
     */
    if (fileInfo.channels == 5) {
        // 5.1 声道映射（不含 LFE）
        ebur128_set_channel(st, 0, EBUR128_LEFT);
        ebur128_set_channel(st, 1, EBUR128_RIGHT);
        ebur128_set_channel(st, 2, EBUR128_CENTER);
        ebur128_set_channel(st, 3, EBUR128_LEFT_SURROUND);
        ebur128_set_channel(st, 4, EBUR128_RIGHT_SURROUND);
    } else if (fileInfo.channels == 6) {
        // 5.1 声道映射（含 LFE，标记为 UNUSED）
        ebur128_set_channel(st, 0, EBUR128_LEFT);
        ebur128_set_channel(st, 1, EBUR128_RIGHT);
        ebur128_set_channel(st, 2, EBUR128_CENTER);
        ebur128_set_channel(st, 3, EBUR128_UNUSED);  // LFE
        ebur128_set_channel(st, 4, EBUR128_LEFT_SURROUND);
        ebur128_set_channel(st, 5, EBUR128_RIGHT_SURROUND);
    }
    // 立体声(2声道)和单声道(1声道)使用默认映射

    /* 4. 分配读取缓冲区
     *    建议每次读取 1 秒的数据
     */
    buffer = (double*)malloc(
        st->samplerate * st->channels * sizeof(double)
    );
    if (!buffer) {
        fileInfoInstance->errorMsgR128 = "Cannot allocate memory";
        ebur128_destroy(&st);
        sf_close(file);
        return;
    }

    /* 5. 读取音频数据并处理
     *    sf_readf_double 返回读取的帧数（不是样本数）
     *    帧数 = 样本数 / 声道数
     */
    int ret;
    while ((framesRead = sf_readf_double(
                file, buffer, (sf_count_t)st->samplerate))) {
        ret = ebur128_add_frames_double(st, buffer, (size_t)framesRead);
        if (ret != EBUR128_SUCCESS) {
            fileInfoInstance->errorMsgR128 = "Error processing audio data";
            goto cleanup;
        }
        if (fileInfoInstance->cancelled)
            goto cleanup;
    }

    /* 6. 获取 LUFS-I (Integrated Loudness) */
    ret = ebur128_loudness_global(st, &fileInfoInstance->lufsI);
    if (ret != EBUR128_SUCCESS) {
        fileInfoInstance->errorMsgR128 = "Error retrieving LUFS-I";
        goto cleanup;
    }

    /* 7. 获取 True Peak（每个声道） */
    for (unsigned int ch = 0; ch < st->channels; ch++) {
        double true_peak;
        ret = ebur128_true_peak(st, ch, &true_peak);
        if (ret != EBUR128_SUCCESS) {
            fileInfoInstance->errorMsgR128 = "Error retrieving True Peak";
            continue;
        }
        
        // 转换为 dBTP (decibels True Peak)
        double true_peak_dbtp = 20.0 * log10(true_peak);
        
        // 记录最大值
        if (true_peak > fileInfoInstance->maxTruePeak) {
            fileInfoInstance->maxTruePeak = true_peak;
        }
    }
    
    fileInfoInstance->maxTruePeak_dBTP = 20.0 * log10(fileInfoInstance->maxTruePeak);

    fileInfoInstance->isR128ParsedOK = true;

cleanup:
    /* 9. 清理资源 */
    if (buffer) {
        free(buffer);
    }
    if (st) {
        ebur128_destroy(&st);
    }
    if (file && sf_close(file)) {
        fileInfoInstance->errorMsgR128 = "Warning: Error closing file";
    }

}
