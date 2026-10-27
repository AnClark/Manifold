#
# Subroutine: Build & Include Dependencies for Manifold
#

# Dear ImGui
add_library (imgui STATIC
    deps/imgui/imgui.cpp
    deps/imgui/imgui_draw.cpp
    deps/imgui/imgui_impl_glfw.cpp
    deps/imgui/imgui_tables.cpp
    deps/imgui/imgui_widgets.cpp
)
if (APPLE)
    # Add Metal implementaion file
    target_sources (imgui PRIVATE deps/imgui/imgui_impl_metal.mm)
else ()
    target_sources (imgui PRIVATE deps/imgui/imgui_impl_opengl2.cpp)
    target_sources (imgui PRIVATE deps/imgui/imgui_impl_opengl3.cpp)
endif ()
target_link_libraries (${PROJECT_NAME} PRIVATE imgui)

# NFD Extended
add_subdirectory (deps/nfd-extended)
include_directories (deps/nfd-extended/src/include)
target_link_libraries (${PROJECT_NAME} PRIVATE nfd)

# libebur128
# NOTICE: Msys2 does not provide static version of libebur128. So I build it myself.
set (CMAKE_POLICY_VERSION_MINIMUM 3.5)  # Deal with legacy code
set (BUILD_SHARED_LIBS OFF)
add_subdirectory (deps/libebur128)
target_link_libraries (${PROJECT_NAME} PRIVATE ebur128)
set (CMAKE_POLICY_VERSION_MINIMUM ${CMAKE_VERSION}) # Rewind CMake version requirement
if (APPLE AND (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" OR CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)arm64(;|$)"))
    # Suppress misleading SSE2 optimization warnings on Apple Silicon
    target_compile_options (ebur128 PRIVATE "-Wno-#warnings")
endif ()

# miniaudio
# Set miniaudio flags
add_compile_definitions (
    MA_ENABLE_WASAPI=1  # Windows
    MA_ENABLE_COREAUDIO=1   # macOS
    MA_ENABLE_PULSEAUDIO=1  # Linux (compatibilty)
    MA_ENABLE_JACK=1    # Linux (pro-audio)
)
# Append miniaudio impl file to source list
target_sources (${PROJECT_NAME} PRIVATE deps/miniaudio/miniaudio_impl.cpp)

# miniaudio custom decoder (libsndfile)
add_library (miniaudio_libsndfile STATIC deps/miniaudio-libsndfile/miniaudio_libsndfile.c)
target_link_libraries (${PROJECT_NAME} PRIVATE miniaudio_libsndfile)

# r8brain-free-src (high-quality resampler)
#
# Core is **header-only**; only the PFFFT_DOUBLE backend needs a compiled .c file.
# R8B_PFFFT_DOUBLE: double-precision PFFFT — same quality as default Ooura FFT,
# but faster (uses SSE2/AVX/NEON intrinsics where available).
add_library (r8brain_pffft_backend STATIC deps/r8brain/fft/pffft_double.c)
target_include_directories (r8brain_pffft_backend PRIVATE deps/r8brain deps/r8brain/fft)
target_compile_definitions (r8brain_pffft_backend PRIVATE R8B_PFFFT_DOUBLE=1)
if (APPLE AND (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$" OR CMAKE_OSX_ARCHITECTURES MATCHES "(^|;)arm64(;|$)"))
    # Explicitly enable ARM Neon optimizations on Apple Silicon
    target_compile_definitions (r8brain_pffft_backend PRIVATE PFFFT_ENABLE_NEON=1)
    target_compile_options (r8brain_pffft_backend PRIVATE "-Wno-#pragma-messages")
endif ()

target_link_libraries (${PROJECT_NAME} PRIVATE r8brain_pffft_backend)
target_compile_definitions (${PROJECT_NAME} PRIVATE R8B_PFFFT_DOUBLE=1)

# libxlsxwriter
set (BUILD_EXAMPLES OFF)
set (BUILD_FUZZERS OFF)
set (BUILD_TESTS OFF)
set (USE_SYSTEM_MINIZIP OFF)
add_subdirectory (deps/libxlsxwriter)
target_link_libraries (${PROJECT_NAME} PRIVATE xlsxwriter)
