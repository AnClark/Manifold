#ifndef miniaudio_libsndfile_c
#define miniaudio_libsndfile_c

#include "miniaudio_libsndfile.h"

#if !defined(MA_NO_LIBSNDFILE)
#include <sndfile.h>
#endif

#include <string.h> /* memset() */
#include <assert.h>


/* =========================================================================
   data_source vtable — dispatch to our public API functions
   ========================================================================= */

static ma_result ma_libsndfile_ds_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    return ma_libsndfile_read_pcm_frames((ma_libsndfile*)pDataSource, pFramesOut, frameCount, pFramesRead);
}

static ma_result ma_libsndfile_ds_seek(ma_data_source* pDataSource, ma_uint64 frameIndex)
{
    return ma_libsndfile_seek_to_pcm_frame((ma_libsndfile*)pDataSource, frameIndex);
}

static ma_result ma_libsndfile_ds_get_data_format(ma_data_source* pDataSource, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    return ma_libsndfile_get_data_format((ma_libsndfile*)pDataSource, pFormat, pChannels, pSampleRate, pChannelMap, channelMapCap);
}

static ma_result ma_libsndfile_ds_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor)
{
    return ma_libsndfile_get_cursor_in_pcm_frames((ma_libsndfile*)pDataSource, pCursor);
}

static ma_result ma_libsndfile_ds_get_length(ma_data_source* pDataSource, ma_uint64* pLength)
{
    return ma_libsndfile_get_length_in_pcm_frames((ma_libsndfile*)pDataSource, pLength);
}

static ma_data_source_vtable g_ma_libsndfile_ds_vtable =
{
    ma_libsndfile_ds_read,
    ma_libsndfile_ds_seek,
    ma_libsndfile_ds_get_data_format,
    ma_libsndfile_ds_get_cursor,
    ma_libsndfile_ds_get_length,
    NULL,   /* onSetLooping */
    0       /* flags */
};


/* =========================================================================
   SF_VIRTUAL_IO bridge callbacks (used by ma_libsndfile_init only)
   =========================================================================
   These adapt miniaudio's onRead/onSeek/onTell callbacks to the interface
   that libsndfile expects from SF_VIRTUAL_IO.
   ========================================================================= */

#if !defined(MA_NO_LIBSNDFILE)

/*
Returns the total byte length of the stream by seeking to the end and back.
Required by libsndfile so it can parse container headers that embed the file
length (e.g. WAV, AIFF).  Returns -1 if the operation fails.
*/
static sf_count_t ma_libsndfile_vio__get_filelen(void* pUserData)
{
    ma_libsndfile* pSndfile = (ma_libsndfile*)pUserData;
    ma_result result;
    ma_int64 savedPos;
    ma_int64 fileLen;

    if (pSndfile->onTell == NULL) {
        return -1;
    }

    /* Save the current byte position. */
    result = pSndfile->onTell(pSndfile->pReadSeekTellUserData, &savedPos);
    if (result != MA_SUCCESS) {
        return -1;
    }

    /* Seek to the very end of the stream. */
    result = pSndfile->onSeek(pSndfile->pReadSeekTellUserData, 0, ma_seek_origin_end);
    if (result != MA_SUCCESS) {
        return -1;
    }

    /* The current byte position is the file length. */
    result = pSndfile->onTell(pSndfile->pReadSeekTellUserData, &fileLen);
    if (result != MA_SUCCESS) {
        return -1;
    }

    /* Restore the original position. */
    pSndfile->onSeek(pSndfile->pReadSeekTellUserData, savedPos, ma_seek_origin_start);

    return (sf_count_t)fileLen;
}

/*
Seeks within the stream and returns the resulting byte offset from the
beginning of the file, as required by libsndfile.  Returns -1 on failure.
*/
static sf_count_t ma_libsndfile_vio__seek(sf_count_t offset, int whence, void* pUserData)
{
    ma_libsndfile* pSndfile = (ma_libsndfile*)pUserData;
    ma_result result;
    ma_seek_origin origin;
    ma_int64 newPos;

    if (whence == SEEK_SET) {
        origin = ma_seek_origin_start;
    } else if (whence == SEEK_END) {
        origin = ma_seek_origin_end;
    } else {
        origin = ma_seek_origin_current;
    }

    result = pSndfile->onSeek(pSndfile->pReadSeekTellUserData, (ma_int64)offset, origin);
    if (result != MA_SUCCESS) {
        return -1;
    }

    /* libsndfile requires the new absolute position as the return value. */
    if (pSndfile->onTell == NULL) {
        return -1;
    }
    result = pSndfile->onTell(pSndfile->pReadSeekTellUserData, &newPos);
    if (result != MA_SUCCESS) {
        return -1;
    }

    return (sf_count_t)newPos;
}

/*
Reads raw bytes from the stream.  Returns the number of bytes actually read,
or 0 on error (consistent with fread() semantics that libsndfile expects).
*/
static sf_count_t ma_libsndfile_vio__read(void* ptr, sf_count_t count, void* pUserData)
{
    ma_libsndfile* pSndfile = (ma_libsndfile*)pUserData;
    ma_result result;
    size_t bytesRead;

    result = pSndfile->onRead(pSndfile->pReadSeekTellUserData, ptr, (size_t)count, &bytesRead);
    if (result != MA_SUCCESS && bytesRead == 0) {
        return 0;
    }

    return (sf_count_t)bytesRead;
}

/*
Write callback — not used because we always open files in SFM_READ mode.
Provided to satisfy the SF_VIRTUAL_IO struct requirement.
*/
static sf_count_t ma_libsndfile_vio__write(const void* ptr, sf_count_t count, void* pUserData)
{
    (void)ptr;
    (void)count;
    (void)pUserData;
    return 0;
}

/*
Returns the current byte offset within the stream.  Returns -1 on failure.
*/
static sf_count_t ma_libsndfile_vio__tell(void* pUserData)
{
    ma_libsndfile* pSndfile = (ma_libsndfile*)pUserData;
    ma_result result;
    ma_int64 cursor;

    if (pSndfile->onTell == NULL) {
        return -1;
    }

    result = pSndfile->onTell(pSndfile->pReadSeekTellUserData, &cursor);
    if (result != MA_SUCCESS) {
        return -1;
    }

    return (sf_count_t)cursor;
}

#endif  /* !MA_NO_LIBSNDFILE */


/* =========================================================================
   Internal initialisation helpers
   ========================================================================= */

/*
Initialises the base data_source and selects the output sample format.
Must be called before any path-specific open (file path or virtual I/O).
*/
static ma_result ma_libsndfile_init_internal(const ma_decoding_backend_config* pConfig, ma_libsndfile* pSndfile)
{
    ma_result result;
    ma_data_source_config dataSourceConfig;

    if (pSndfile == NULL) {
        return MA_INVALID_ARGS;
    }

    memset(pSndfile, 0, sizeof(*pSndfile));
    pSndfile->format = ma_format_f32;   /* f32 by default. */

    if (pConfig != NULL) {
        if (pConfig->preferredFormat == ma_format_f32  ||
            pConfig->preferredFormat == ma_format_s16  ||
            pConfig->preferredFormat == ma_format_s32) {
            pSndfile->format = pConfig->preferredFormat;
        }
        /* Any other format is silently ignored; f32 is used instead. */
    }

    dataSourceConfig = ma_data_source_config_init();
    dataSourceConfig.vtable = &g_ma_libsndfile_ds_vtable;

    result = ma_data_source_init(&dataSourceConfig, &pSndfile->ds);
    if (result != MA_SUCCESS) {
        return result;  /* Failed to initialise base data source. */
    }

    return MA_SUCCESS;
}

/*
Caches the stream properties (channels, sample rate, total frames) returned
by libsndfile in SF_INFO after a successful open.
*/
#if !defined(MA_NO_LIBSNDFILE)
static void ma_libsndfile_cache_info(ma_libsndfile* pSndfile, const SF_INFO* pInfo)
{
    pSndfile->channels    = (ma_uint32)pInfo->channels;
    pSndfile->sampleRate  = (ma_uint32)pInfo->samplerate;
    pSndfile->totalFrames = (ma_uint64)pInfo->frames;
}
#endif


/* =========================================================================
   Public API
   ========================================================================= */

MA_API ma_result ma_libsndfile_init(ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libsndfile* pSndfile)
{
    ma_result result;

    (void)pAllocationCallbacks;

    /*
    onRead and onSeek are mandatory.  onTell is also required here because the
    SF_VIRTUAL_IO layer needs it for seek() and get_filelen().
    */
    if (onRead == NULL || onSeek == NULL || onTell == NULL) {
        return MA_INVALID_ARGS;
    }

    result = ma_libsndfile_init_internal(pConfig, pSndfile);
    if (result != MA_SUCCESS) {
        return result;
    }

    pSndfile->onRead                = onRead;
    pSndfile->onSeek                = onSeek;
    pSndfile->onTell                = onTell;
    pSndfile->pReadSeekTellUserData = pReadSeekTellUserData;

#if !defined(MA_NO_LIBSNDFILE)
    {
        SF_INFO sfInfo;
        SF_VIRTUAL_IO sfVio;

        memset(&sfInfo, 0, sizeof(sfInfo));

        sfVio.get_filelen = ma_libsndfile_vio__get_filelen;
        sfVio.seek        = ma_libsndfile_vio__seek;
        sfVio.read        = ma_libsndfile_vio__read;
        sfVio.write       = ma_libsndfile_vio__write;
        sfVio.tell        = ma_libsndfile_vio__tell;

        pSndfile->sndfile = sf_open_virtual(&sfVio, SFM_READ, &sfInfo, pSndfile);
        if (pSndfile->sndfile == NULL) {
            ma_data_source_uninit(&pSndfile->ds);
            return MA_INVALID_FILE;
        }

        ma_libsndfile_cache_info(pSndfile, &sfInfo);
        return MA_SUCCESS;
    }
#else
    {
        /* libsndfile is disabled. */
        ma_data_source_uninit(&pSndfile->ds);
        return MA_NOT_IMPLEMENTED;
    }
#endif
}

MA_API ma_result ma_libsndfile_init_file(const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_libsndfile* pSndfile)
{
    ma_result result;

    (void)pAllocationCallbacks;

    result = ma_libsndfile_init_internal(pConfig, pSndfile);
    if (result != MA_SUCCESS) {
        return result;
    }

#if !defined(MA_NO_LIBSNDFILE)
    {
        SF_INFO sfInfo;

        memset(&sfInfo, 0, sizeof(sfInfo));

        pSndfile->sndfile = sf_open(pFilePath, SFM_READ, &sfInfo);
        if (pSndfile->sndfile == NULL) {
            ma_data_source_uninit(&pSndfile->ds);
            return MA_INVALID_FILE;
        }

        ma_libsndfile_cache_info(pSndfile, &sfInfo);
        return MA_SUCCESS;
    }
#else
    {
        /* libsndfile is disabled. */
        (void)pFilePath;
        ma_data_source_uninit(&pSndfile->ds);
        return MA_NOT_IMPLEMENTED;
    }
#endif
}

MA_API void ma_libsndfile_uninit(ma_libsndfile* pSndfile, const ma_allocation_callbacks* pAllocationCallbacks)
{
    if (pSndfile == NULL) {
        return;
    }

    (void)pAllocationCallbacks;

#if !defined(MA_NO_LIBSNDFILE)
    {
        sf_close((SNDFILE*)pSndfile->sndfile);
    }
#else
    {
        /* libsndfile is disabled. Should never be reached if init failed. */
        assert(MA_FALSE);
    }
#endif

    ma_data_source_uninit(&pSndfile->ds);
}

MA_API ma_result ma_libsndfile_read_pcm_frames(ma_libsndfile* pSndfile, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead)
{
    if (pFramesRead != NULL) {
        *pFramesRead = 0;
    }

    if (frameCount == 0) {
        return MA_INVALID_ARGS;
    }

    if (pSndfile == NULL) {
        return MA_INVALID_ARGS;
    }

#if !defined(MA_NO_LIBSNDFILE)
    {
        sf_count_t framesActuallyRead;

        /*
        sf_readf_* reads up to frameCount interleaved frames and returns the
        actual number read.  A return value less than frameCount means EOF.
        All three variants produce interleaved output directly — no extra
        de-interleaving step is needed (unlike libvorbis's ov_read_float).
        */
        if (pSndfile->format == ma_format_s16) {
            framesActuallyRead = sf_readf_short((SNDFILE*)pSndfile->sndfile, (short*)pFramesOut, (sf_count_t)frameCount);
        } else if (pSndfile->format == ma_format_s32) {
            framesActuallyRead = sf_readf_int((SNDFILE*)pSndfile->sndfile, (int*)pFramesOut, (sf_count_t)frameCount);
        } else {
            /* Default: f32 */
            framesActuallyRead = sf_readf_float((SNDFILE*)pSndfile->sndfile, (float*)pFramesOut, (sf_count_t)frameCount);
        }

        if (pFramesRead != NULL) {
            *pFramesRead = (ma_uint64)framesActuallyRead;
        }

        if (framesActuallyRead == 0) {
            return MA_AT_END;
        }

        return MA_SUCCESS;
    }
#else
    {
        /* libsndfile is disabled. Should never be reached if init failed. */
        assert(MA_FALSE);

        (void)pFramesOut;
        (void)frameCount;
        (void)pFramesRead;

        return MA_NOT_IMPLEMENTED;
    }
#endif
}

MA_API ma_result ma_libsndfile_seek_to_pcm_frame(ma_libsndfile* pSndfile, ma_uint64 frameIndex)
{
    if (pSndfile == NULL) {
        return MA_INVALID_ARGS;
    }

#if !defined(MA_NO_LIBSNDFILE)
    {
        sf_count_t result = sf_seek((SNDFILE*)pSndfile->sndfile, (sf_count_t)frameIndex, SEEK_SET);
        if (result == -1) {
            return MA_INVALID_OPERATION;    /* Stream may not be seekable. */
        }

        return MA_SUCCESS;
    }
#else
    {
        assert(MA_FALSE);

        (void)frameIndex;

        return MA_NOT_IMPLEMENTED;
    }
#endif
}

MA_API ma_result ma_libsndfile_get_data_format(ma_libsndfile* pSndfile, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel* pChannelMap, size_t channelMapCap)
{
    /* Safe defaults. */
    if (pFormat != NULL) {
        *pFormat = ma_format_unknown;
    }
    if (pChannels != NULL) {
        *pChannels = 0;
    }
    if (pSampleRate != NULL) {
        *pSampleRate = 0;
    }
    if (pChannelMap != NULL) {
        memset(pChannelMap, 0, sizeof(*pChannelMap) * channelMapCap);
    }

    if (pSndfile == NULL) {
        return MA_INVALID_OPERATION;
    }

    if (pFormat != NULL) {
        *pFormat = pSndfile->format;
    }

    if (pChannels != NULL) {
        *pChannels = pSndfile->channels;
    }

    if (pSampleRate != NULL) {
        *pSampleRate = pSndfile->sampleRate;
    }

    if (pChannelMap != NULL) {
        /*
        libsndfile uses the Microsoft/WAVE channel ordering convention, which
        matches WAVEFORMATEXTENSIBLE.  This is the same mapping used by miniaudio
        for WAV files, so it is the most accurate choice here.
        */
        ma_channel_map_init_standard(ma_standard_channel_map_microsoft, pChannelMap, channelMapCap, pSndfile->channels);
    }

    return MA_SUCCESS;
}

MA_API ma_result ma_libsndfile_get_cursor_in_pcm_frames(ma_libsndfile* pSndfile, ma_uint64* pCursor)
{
    if (pCursor == NULL) {
        return MA_INVALID_ARGS;
    }

    *pCursor = 0;   /* Safety. */

    if (pSndfile == NULL) {
        return MA_INVALID_ARGS;
    }

#if !defined(MA_NO_LIBSNDFILE)
    {
        /*
        sf_seek with SEEK_CUR and offset 0 returns the current read position
        in frames without moving the read pointer.
        */
        sf_count_t pos = sf_seek((SNDFILE*)pSndfile->sndfile, 0, SEEK_CUR);
        if (pos == -1) {
            return MA_INVALID_FILE;
        }

        *pCursor = (ma_uint64)pos;
        return MA_SUCCESS;
    }
#else
    {
        assert(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
#endif
}

MA_API ma_result ma_libsndfile_get_length_in_pcm_frames(ma_libsndfile* pSndfile, ma_uint64* pLength)
{
    if (pLength == NULL) {
        return MA_INVALID_ARGS;
    }

    *pLength = 0;   /* Safety. */

    if (pSndfile == NULL) {
        return MA_INVALID_ARGS;
    }

#if !defined(MA_NO_LIBSNDFILE)
    {
        /* sfInfo.frames was cached at open time; no extra I/O required. */
        *pLength = pSndfile->totalFrames;
        return MA_SUCCESS;
    }
#else
    {
        assert(MA_FALSE);
        return MA_NOT_IMPLEMENTED;
    }
#endif
}


/* =========================================================================
   Decoding backend vtable
   Plug ma_decoding_backend_libsndfile into ma_decoder_config.pBackendVTables.
   ========================================================================= */

#if !defined(MA_NO_LIBSNDFILE)

static ma_result ma_decoding_backend_init__libsndfile(void* pUserData, ma_read_proc onRead, ma_seek_proc onSeek, ma_tell_proc onTell, void* pReadSeekTellUserData, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libsndfile* pSndfile;

    (void)pUserData;

    pSndfile = (ma_libsndfile*)ma_malloc(sizeof(*pSndfile), pAllocationCallbacks);
    if (pSndfile == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libsndfile_init(onRead, onSeek, onTell, pReadSeekTellUserData, pConfig, pAllocationCallbacks, pSndfile);
    if (result != MA_SUCCESS) {
        ma_free(pSndfile, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pSndfile;
    return MA_SUCCESS;
}

static ma_result ma_decoding_backend_init_file__libsndfile(void* pUserData, const char* pFilePath, const ma_decoding_backend_config* pConfig, const ma_allocation_callbacks* pAllocationCallbacks, ma_data_source** ppBackend)
{
    ma_result result;
    ma_libsndfile* pSndfile;

    (void)pUserData;

    pSndfile = (ma_libsndfile*)ma_malloc(sizeof(*pSndfile), pAllocationCallbacks);
    if (pSndfile == NULL) {
        return MA_OUT_OF_MEMORY;
    }

    result = ma_libsndfile_init_file(pFilePath, pConfig, pAllocationCallbacks, pSndfile);
    if (result != MA_SUCCESS) {
        ma_free(pSndfile, pAllocationCallbacks);
        return result;
    }

    *ppBackend = pSndfile;
    return MA_SUCCESS;
}

static void ma_decoding_backend_uninit__libsndfile(void* pUserData, ma_data_source* pBackend, const ma_allocation_callbacks* pAllocationCallbacks)
{
    ma_libsndfile* pSndfile = (ma_libsndfile*)pBackend;

    (void)pUserData;

    ma_libsndfile_uninit(pSndfile, pAllocationCallbacks);
    ma_free(pSndfile, pAllocationCallbacks);
}

static ma_decoding_backend_vtable ma_gDecodingBackendVTable_libsndfile =
{
    ma_decoding_backend_init__libsndfile,
    ma_decoding_backend_init_file__libsndfile,
    NULL,   /* onInitFileW */
    NULL,   /* onInitMemory */
    ma_decoding_backend_uninit__libsndfile
};

ma_decoding_backend_vtable* ma_decoding_backend_libsndfile = &ma_gDecodingBackendVTable_libsndfile;

#else

ma_decoding_backend_vtable* ma_decoding_backend_libsndfile = NULL;

#endif  /* !MA_NO_LIBSNDFILE */

#endif  /* miniaudio_libsndfile_c */
