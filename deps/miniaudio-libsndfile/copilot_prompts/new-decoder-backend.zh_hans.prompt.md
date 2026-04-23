---
description: "为指定音频库适配一个 miniaudio 自定义解码后端（生成 .h + .c 文件对）"
argument-hint: "目标音频库名称，例如：libsndfile、libavcodec"
agent: "agent"
---

根据对话中已有的 [miniaudio_libsndfile.h](../../MY_DECODER/miniaudio_libsndfile.h) 与 [miniaudio_libsndfile.c](../../MY_DECODER/miniaudio_libsndfile.c) 作为最新参考实现，以及 [extras/decoders/libopus/miniaudio_libopus.c](../../extras/decoders/libopus/miniaudio_libopus.c) 和 [extras/decoders/libvorbis/miniaudio_libvorbis.c](../../extras/decoders/libvorbis/miniaudio_libvorbis.c) 作为官方示例，为 **$ARGUMENTS** 编写新的 miniaudio 解码后端。

## 任务要求

生成两个文件，放置在工作区 `MY_DECODER/` 目录下：

- `miniaudio_<库名>.h` — 公开 API 与结构体声明
- `miniaudio_<库名>.c` — 完整实现

## 必须遵守的结构规范

### 结构体
- 首成员必须是 `ma_data_source_base ds`，使其可直接用于 `ma_data_source_*()` API
- 用 `void*` 持有库的句柄（如 `void* handle`），避免在 `.h` 中引入库的头文件依赖
- 缓存打开时已知的 `channels`、`sampleRate`、`totalFrames`，避免运行期反复查询

### 实现文件结构（按顺序）
1. `#ifndef` 防重复编译守卫
2. `#include` 宏守卫：`#if !defined(MA_NO_<库名大写>)` 包裹库头文件
3. `ma_data_source_vtable` 的五个分发函数（`ds_read` / `ds_seek` / `ds_get_data_format` / `ds_get_cursor` / `ds_get_length`）
4. I/O 回调桥接函数（将库的 I/O 接口适配为 `ma_read_proc` / `ma_seek_proc` / `ma_tell_proc`）
5. `ma_libXxx_init_internal()`（初始化 `ma_data_source_base`，设置 format）
6. `ma_libXxx_init()`（流模式，使用回调）
7. `ma_libXxx_init_file()`（文件路径模式，优先使用）
8. `ma_libXxx_uninit()`
9. `ma_libXxx_read_pcm_frames()`
10. `ma_libXxx_seek_to_pcm_frame()`
11. `ma_libXxx_get_data_format()`
12. `ma_libXxx_get_cursor_in_pcm_frames()`
13. `ma_libXxx_get_length_in_pcm_frames()`
14. vtable 实现与 `ma_decoding_backend_libXxx` 导出

### 编译守卫约定
- 所有实现用 `#if !defined(MA_NO_<库名大写>)` 包裹
- 禁用分支必须提供 `assert(MA_FALSE)` 或 `(void)` 填充，并返回 `MA_NOT_IMPLEMENTED`
- `MA_NO_<库名大写>` 被定义时，将 `ma_decoding_backend_libXxx` 置为 `NULL`

### 格式选择
- 默认 `ma_format_f32`
- 从 `pConfig->preferredFormat` 读取偏好，至少支持 `f32` 和 `s16`；能支持 `s32` 更好
- 在 `init_internal` 中选择，后续所有函数直接读结构体字段，不再判断 `pConfig`

### 返回值规范
- `read_pcm_frames`：读到 0 帧时返回 `MA_AT_END`
- `seek_to_pcm_frame`：不可 seek 时返回 `MA_INVALID_OPERATION`
- 所有函数在 `pXxx == NULL` 时返回 `MA_INVALID_ARGS` 或 `MA_INVALID_OPERATION`

### vtable 注册
- 提供 `onInit`（流模式）和 `onInitFile`（路径模式）
- `onInitFileW` 与 `onInitMemory` 填 `NULL`（除非该库原生支持）
- 导出 `extern ma_decoding_backend_vtable* ma_decoding_backend_libXxx`

## 调用方式（供用户参考）

```c
#include "miniaudio_lib<name>.h"
#include "miniaudio_lib<name>.c"

ma_decoding_backend_vtable* pBackends[] = { ma_decoding_backend_lib<name> };
ma_decoder_config cfg = ma_decoder_config_init_default();
cfg.pBackendVTables = pBackends;
cfg.backendCount    = 1;

ma_decoder_init_file(argv[1], &cfg, &decoder);
```

## 完成后
1. 检查 `.h` 与 `.c` 的 `#ifndef` 守卫名称一致
2. 用 `gcc -fsyntax-only` 做语法检查，确保零错误
3. 简要说明该库的 I/O 回调桥接与 libsndfile/libopus 的差异点
