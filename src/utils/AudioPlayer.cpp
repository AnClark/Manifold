#include "AudioPlayer.hpp"

#include <cstring>
#include <filesystem>

#include "miniaudio_libsndfile.h"

AudioPlayer::AudioPlayer() : isFileLoaded(false), isDeviceInitialized(false), isPlaying(false)
{
    // Register custom backend based on libsndfile.
    // Configure backend list
    pBackends = { ma_decoding_backend_libsndfile };
    
    // Configure decoder to use the custom backend list.
    decoderConfig = ma_decoder_config_init_default();
    decoderConfig.pCustomBackendUserData = nullptr; // Optional user data for custom backends, not used in this example.
    decoderConfig.ppCustomBackendVTables = pBackends.data();
    decoderConfig.customBackendCount    = static_cast<ma_uint32>(pBackends.size());
}

void AudioPlayer::loadAudioFile(const char* filePath)
{
    // Stop previous playback and unload previous file if any
    if (isFileLoaded || isDeviceInitialized || isPlaying)
        cleanUp();

    errorMsg.clear();

#ifdef _WIN32
    // On Windows, fopen (used internally by miniaudio) uses the ANSI code page and cannot
    // open UTF-8 paths with CJK/non-ASCII characters. Convert to wchar_t* and use the
    // wide-char variant instead.
    std::wstring wPath = std::filesystem::u8path(filePath).wstring();
    result = ma_decoder_init_file_w(wPath.c_str(), &decoderConfig, &decoder);
#else
    result = ma_decoder_init_file(filePath, &decoderConfig, &decoder);
#endif
    if (result != MA_SUCCESS) {
        errorMsg = std::string("Could not load file: ") + filePath + "\n";
        return;
    }

    deviceConfig = ma_device_config_init(ma_device_type_playback);
    deviceConfig.playback.format   = decoder.outputFormat;
    deviceConfig.playback.channels = decoder.outputChannels;
    deviceConfig.sampleRate        = decoder.outputSampleRate;
    deviceConfig.dataCallback      = audioDataCallback;
    deviceConfig.pUserData         = this;

    isFileLoaded = true;
}

void AudioPlayer::cleanUp()
{
    // Do not clear error message here, so users can know what happened when error occurs (if initDevice() or play() fails).
    // errorMsg.clear();

    if (isDeviceInitialized || isPlaying)
    {
        ma_device_uninit(&device);
        isPlaying = false;
        isDeviceInitialized = false;
        isEOF = false;
    }

    if (isFileLoaded)
    {
        ma_decoder_uninit(&decoder);
        isFileLoaded = false;
    }
}

void AudioPlayer::initDevice()
{
    if (!isFileLoaded)
        // Do not clear error message here, so users can know what happened on loadAudioFile().
        return;

    errorMsg.clear();

    if (ma_device_init(NULL, &deviceConfig, &device) != MA_SUCCESS) {
        errorMsg = "Failed to open playback device.";
        cleanUp();
        return;
    }

    isDeviceInitialized = true;
}

void AudioPlayer::play()
{
    if (!isFileLoaded || !isDeviceInitialized || isPlaying)
        // Do not clear error message here, so users can know what happened on loadAudioFile().
        return;

    errorMsg.clear();

    if (ma_device_start(&device) != MA_SUCCESS) {
        errorMsg = "Failed to start playback device.";
        cleanUp();
        return;
    }

    isPlaying = true;
    isEOF = false;
}

void AudioPlayer::pause()
{
    if (!isFileLoaded || !isDeviceInitialized || !isPlaying)
        return;

    errorMsg.clear();

    if (ma_device_stop(&device) != MA_SUCCESS) {
        errorMsg = "Failed to stop playback device.";
        cleanUp();
        return;
    }

    isPlaying = false;
}

void AudioPlayer::stop()
{
    errorMsg.clear();
    cleanUp();
}

void AudioPlayer::audioDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount)
{
    AudioPlayer* pPlayer = (AudioPlayer*)pDevice->pUserData;
    if (pPlayer == NULL) {
        return;
    }

    ma_uint64 framesRead = 0;
    ma_decoder_read_pcm_frames(&pPlayer->decoder, pOutput, frameCount, &framesRead);

    // If the actual number of frames read is less than the requested amount, it indicates that the file has reached the end of playback.
    if (framesRead < frameCount) {
        // Zero out the remaining output buffer to avoid noise
        ma_uint32 bytesPerFrame = ma_get_bytes_per_frame(pPlayer->decoder.outputFormat, pPlayer->decoder.outputChannels);
        memset((ma_uint8*)pOutput + framesRead * bytesPerFrame, 0, (frameCount - framesRead) * bytesPerFrame);

        pPlayer->isPlaying = false;
        pPlayer->isEOF = true;
    }

    (void)pInput;
}
