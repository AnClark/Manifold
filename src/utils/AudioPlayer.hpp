#pragma once

#include "miniaudio.h"
#include <string>

class AudioPlayer
{
public:
    AudioPlayer() : isFileLoaded(false), isDeviceInitialized(false), isPlaying(false) {}
    ~AudioPlayer()
    {
        cleanUp();
    }

    void loadAudioFile(const char* fileName);
    void cleanUp();

    const char* getErrorMsg() { return errorMsg.c_str(); }
    bool hasError() { return !errorMsg.empty(); }

    void initDevice();
    void play();
    void pause();
    void stop();

    static void audioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount);

private:
    bool isFileLoaded;
    bool isDeviceInitialized;
    bool isPlaying;

    ma_result result;
    ma_decoder decoder;
    ma_device_config deviceConfig;
    ma_device device;

    std::string errorMsg;
};
