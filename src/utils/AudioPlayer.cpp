#include "AudioPlayer.hpp"

#include <filesystem>

void AudioPlayer::loadAudioFile(const char* fileName)
{
    // Stop previous playback and unload previous file if any
    if (isFileLoaded || isPlaybackActive)
        cleanUp();

    errorMsg.clear();

#ifdef _WIN32
    // On Windows, fopen (used internally by miniaudio) uses the ANSI code page and cannot
    // open UTF-8 paths with CJK/non-ASCII characters. Convert to wchar_t* and use the
    // wide-char variant instead.
    std::wstring wPath = std::filesystem::u8path(fileName).wstring();
    result = ma_decoder_init_file_w(wPath.c_str(), NULL, &decoder);
#else
    result = ma_decoder_init_file(fileName, NULL, &decoder);
#endif
    if (result != MA_SUCCESS) {
        errorMsg = std::string("Could not load file: ") + fileName + "\n";
        return;
    }

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = decoder.outputFormat;
    deviceConfig.playback.channels = decoder.outputChannels;
    deviceConfig.sampleRate        = decoder.outputSampleRate;
    deviceConfig.dataCallback      = audioDataCallback;
    deviceConfig.pUserData         = &decoder;

    isFileLoaded = true;
}

void AudioPlayer::cleanUp()
{
    // Do not clear error message here, so users can know what happened when error occurs (if initDevice() or play() fails).
    // errorMsg.clear();

    if (isPlaybackActive)
    {
        ma_device_uninit(&device);
        isPlaybackActive = false;
    }

    if (isFileLoaded)
    {
        ma_decoder_uninit(&decoder);
        isFileLoaded = false;
    }
}

void AudioPlayer::play()
{
    if (!isFileLoaded)
        // Do not clear error message here, so users can know what happened on loadAudioFile().
        return;

    errorMsg.clear();

    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        errorMsg = "Failed to open playback device.";
        ma_decoder_uninit(&decoder);
        return;
    }

    if (ma_device_start(&device) != MA_SUCCESS) {
        errorMsg = "Failed to start playback device.";
        ma_device_uninit(&device);
        ma_decoder_uninit(&decoder);
        return;
    }

    isPlaybackActive = true;
}

void AudioPlayer::audioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    ma_decoder* pDecoder = (ma_decoder*)pDevice->pUserData;
    if (pDecoder == NULL) {
        return;
    }

    ma_decoder_read_pcm_frames(pDecoder, pOutput, frameCount, NULL);

    (void)pInput;
}
