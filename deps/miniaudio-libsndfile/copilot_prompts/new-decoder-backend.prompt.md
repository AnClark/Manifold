---
description: "Adapt a miniaudio custom decoding backend for a given audio library (generates a .h + .c file pair)"
argument-hint: "Target audio library name, e.g. libsndfile, libavcodec"
agent: "agent"
---

Using [miniaudio_libsndfile.h](../../MY_DECODER/miniaudio_libsndfile.h) and [miniaudio_libsndfile.c](../../MY_DECODER/miniaudio_libsndfile.c) as the most up-to-date reference implementation, and [extras/decoders/libopus/miniaudio_libopus.c](../../extras/decoders/libopus/miniaudio_libopus.c) and [extras/decoders/libvorbis/miniaudio_libvorbis.c](../../extras/decoders/libvorbis/miniaudio_libvorbis.c) as the official examples, write a new miniaudio decoding backend for **$ARGUMENTS**.

## Task

Generate two files under the `MY_DECODER/` directory in this workspace:

- `miniaudio_<libname>.h` — public API and struct declarations
- `miniaudio_<libname>.c` — full implementation

## Structural Rules

### Struct layout
- The first member must be `ma_data_source_base ds` so the object can be used directly with `ma_data_source_*()`.
- Hold the library handle as `void*` to avoid pulling the library's own headers into the `.h` file.
- Cache `channels`, `sampleRate`, and `totalFrames` at open time to avoid repeated queries at runtime.

### Implementation file order
1. `#ifndef` include guard
2. Library header wrapped in `#if !defined(MA_NO_<LIBNAME_UPPER>)`
3. Five `ma_data_source_vtable` dispatch functions (`ds_read` / `ds_seek` / `ds_get_data_format` / `ds_get_cursor` / `ds_get_length`)
4. I/O bridge callbacks (adapt the library's I/O interface to `ma_read_proc` / `ma_seek_proc` / `ma_tell_proc`)
5. `ma_libXxx_init_internal()` — initialise `ma_data_source_base` and select format
6. `ma_libXxx_init()` — stream mode via callbacks
7. `ma_libXxx_init_file()` — file-path mode (preferred for typical playback)
8. `ma_libXxx_uninit()`
9. `ma_libXxx_read_pcm_frames()`
10. `ma_libXxx_seek_to_pcm_frame()`
11. `ma_libXxx_get_data_format()`
12. `ma_libXxx_get_cursor_in_pcm_frames()`
13. `ma_libXxx_get_length_in_pcm_frames()`
14. vtable implementation and `ma_decoding_backend_libXxx` export

### Compile-time guard conventions
- Wrap all implementation code in `#if !defined(MA_NO_<LIBNAME_UPPER>)`.
- Disabled branches must contain `assert(MA_FALSE)` (or `(void)` suppressions) and return `MA_NOT_IMPLEMENTED`.
- When `MA_NO_<LIBNAME_UPPER>` is defined, set `ma_decoding_backend_libXxx` to `NULL`.

### Format selection
- Default to `ma_format_f32`.
- Honour `pConfig->preferredFormat`; at minimum support `f32` and `s16`, ideally also `s32`.
- Choose the format once in `init_internal`; all subsequent functions read the cached field — never re-inspect `pConfig`.

### Return-value conventions
- `read_pcm_frames`: return `MA_AT_END` when 0 frames are decoded.
- `seek_to_pcm_frame`: return `MA_INVALID_OPERATION` for non-seekable streams.
- All functions must return `MA_INVALID_ARGS` or `MA_INVALID_OPERATION` when the object pointer is `NULL`.

### vtable registration
- Provide `onInit` (stream mode) and `onInitFile` (path mode).
- Leave `onInitFileW` and `onInitMemory` as `NULL` unless the library natively supports them.
- Export `extern ma_decoding_backend_vtable* ma_decoding_backend_libXxx`.

## Usage snippet (for reference)

```c
#include "miniaudio_lib<name>.h"
#include "miniaudio_lib<name>.c"

ma_decoding_backend_vtable* pBackends[] = { ma_decoding_backend_lib<name> };
ma_decoder_config cfg = ma_decoder_config_init_default();
cfg.pBackendVTables = pBackends;
cfg.backendCount    = 1;

ma_decoder_init_file(argv[1], &cfg, &decoder);
```

## After generating the files
1. Verify that the `#ifndef` guard names in `.h` and `.c` match.
2. Run `gcc -fsyntax-only` to confirm zero errors.
3. Briefly explain how the library's I/O bridge differs from the libsndfile / libopus approach.
