/*
This implements a data source that decodes audio files via libsndfile.

Supported formats include (but are not limited to): WAV, AIFF, AU/SND, FLAC,
CAF, W64, RF64, and many others that libsndfile supports.

This object can be plugged into any `ma_data_source_*()` API and can also be
used as a custom decoding backend. See the custom_decoder example.

USAGE
=====
1. Include this header after miniaudio.h:

       #define MINIAUDIO_IMPLEMENTATION
       #include "miniaudio.h"
       #include "miniaudio_libsndfile.h"

2. In exactly one .c file, include the implementation:

       #include "miniaudio_libsndfile.c"

3. Register the backend when initialising a decoder:

       ma_decoding_backend_vtable* pCustomBackendVTables[] = {
           ma_decoding_backend_libsndfile
       };

       ma_decoder_config config = ma_decoder_config_init_default();
       config.ppCustomBackendVTables = pCustomBackendVTables;
       config.customBackendCount     = 1;

       ma_decoder_init_file("sound.aiff", &config, &decoder);

COMPILE-TIME SWITCHES
=====================
  MA_NO_LIBSNDFILE   Disables the libsndfile backend. The vtable pointer
                     will be set to NULL so miniaudio can skip it silently.

DEPENDENCIES
============
  libsndfile (https://libsndfile.github.io/libsndfile/)
  Link with: -lsndfile
*/

#ifndef miniaudio_libsndfile_h
#define miniaudio_libsndfile_h

#ifdef __cplusplus
extern "C" {
#endif

#include <miniaudio.h>

typedef struct
{
    ma_data_source_base ds;         /* Must be the first member — allows direct use as a ma_data_source. */
    ma_read_proc  onRead;
    ma_seek_proc  onSeek;
    ma_tell_proc  onTell;
    void*         pReadSeekTellUserData;
    ma_format     format;           /* ma_format_f32 (default), ma_format_s16, or ma_format_s32. */
    ma_uint32     channels;         /* Cached from SF_INFO at open time. */
    ma_uint32     sampleRate;       /* Cached from SF_INFO at open time. */
    ma_uint64     totalFrames;      /* Cached from SF_INFO at open time. */
    /*SNDFILE**/  void* sndfile;    /* Opaque pointer — avoids a sndfile.h dependency in this header. */
} ma_libsndfile;

/*
Initialise a libsndfile decoder from miniaudio I/O callbacks (stream mode).
onRead and onSeek are mandatory; onTell must also be non-NULL because libsndfile
needs it to implement its virtual I/O layer.
*/
MA_API ma_result ma_libsndfile_init(ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libsndfile* pSndfile);

/* Initialise a libsndfile decoder directly from a file path (preferred for file playback). */
MA_API ma_result ma_libsndfile_init_file(const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libsndfile* pSndfile);

MA_API void      ma_libsndfile_uninit(ma_libsndfile* pSndfile, const ma_allocation_callbacks* pAllocationCallbacks);

MA_API ma_result ma_libsndfile_read_pcm_frames(ma_libsndfile* pSndfile, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead);
MA_API ma_result ma_libsndfile_seek_to_pcm_frame(ma_libsndfile* pSndfile, ma_uint64 frameIndex);
MA_API ma_result ma_libsndfile_get_data_format(ma_libsndfile* pSndfile, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap);
MA_API ma_result ma_libsndfile_get_cursor_in_pcm_frames(ma_libsndfile* pSndfile, ma_uint64* pCursor);
MA_API ma_result ma_libsndfile_get_length_in_pcm_frames(ma_libsndfile* pSndfile, ma_uint64* pLength);

/* Plug this into ma_decoder_config.pBackendVTables[]. No user data required. */
extern ma_decoding_backend_vtable* ma_decoding_backend_libsndfile;

#ifdef __cplusplus
}
#endif
#endif  /* miniaudio_libsndfile_h */
