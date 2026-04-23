#pragma once

#include "miniaudio.h"
#include <string>
#include <array>

class AudioPlayer
{
public:
    AudioPlayer();
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

    std::array<ma_decoding_backend_vtable*, 1> pBackends;
    ma_decoder_config decoderConfig;

    std::string errorMsg;
};
