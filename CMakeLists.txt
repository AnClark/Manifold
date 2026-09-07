cmake_minimum_required (VERSION 3.18)

project (Manifold
    VERSION 0.1.0
)

#
# macOS stuff.
#
# On macOS, Manifold renders UI with Metal. Special treats required.
# See: `if (APPLE)` blocks in this CMakeLists.txt.
#

if (APPLE)
    enable_language (OBJCXX)
    set (IMGUI_APPLICATION_PLATFORM_SOURCE deps/imgui-application/ApplicationMetal.mm)
    set_source_files_properties (
        deps/imgui-application/ApplicationMetal.mm
        deps/imgui/imgui_impl_metal.mm
        PROPERTIES COMPILE_OPTIONS "-fobjc-arc"
    )
else ()
    set (IMGUI_APPLICATION_PLATFORM_SOURCE)
endif ()

#
# Global config
#

# Specify C++ standard
set (CMAKE_CXX_STANDARD 17)

# Prefer static libraries on Windows (prefer ".a" if user is using GCC/Clang)
if (WIN32)
    set (CMAKE_FIND_LIBRARY_SUFFIXES ".a;.lib;.dll.a;.dll")
endif ()

# Link against static libgcc runtime libraries on Win32 (by simply passing -static; compatible with Clang)
# NOTICE: - Do NOT pass -static on Linux - it will only link against static libraries,
#           even though no static version available!
#         - On Windows, GCC/ld won't link against static libs if you only specify -static.
#           Only CMAKE_FIND_LIBRARY_SUFFIXES works.
if (WIN32 AND NOT MSVC)
    add_link_options (-static)
endif ()

# Enable PIC on non-MSVC compilers (essential for linking against our own static libs)
if (NOT MSVC)
    add_compile_options (-fPIC)
endif ()

# Common include dirs
include_directories (
    src
    deps/imgui
    deps/imgui-application
    deps/libebur128/ebur128
    deps/miniaudio
    deps/miniaudio-libsndfile
    deps/r8brain
    deps/ImGuiNotify
    deps/ImGuiNotify/fonts
    deps/toml
)

# Load pkg-config for finding dependencies, and let it prefer finding static libraries
find_package (PkgConfig REQUIRED)
set(PKG_CONFIG_ARGN "--static")

#
# Feature options (can be toggled during CMake configuration)
#

option (MANIFOLD_ENABLE_PARALLEL_ANALYZING "Enable analyzing audio file in parallel (EBU R128, DC Offset)" ON)
set (MANIFOLD_EBUR128_ANALYZER_THREADS_DEFAULT 4 CACHE STRING "Specify the default thread count of EBU R128 parallel analysis.")
set (MANIFOLD_DCOFFSET_ANALYZER_THREADS_DEFAULT 4 CACHE STRING "Specify the default thread count of Dc Offset parallel analysis.")

#
# Build our main application
#

add_executable (${PROJECT_NAME} WIN32
    ${SRC_IMGUI}
    deps/imgui-application/Application.cpp
    ${IMGUI_APPLICATION_PLATFORM_SOURCE}   # Platform-specific ImGui implementation file (currently only macOS requires this)
    src/Main.cpp
    src/utils/LogManager.cpp
    src/base/SndFileInfo.cpp
    src/base/Worker.cpp
    src/workers/SndFileWorker.cpp
    src/workers/EBUR128Worker.cpp
    src/workers/DcOffsetWorker.cpp
    src/workers/SingleFileProcessorWorker.cpp
    src/dispatchers/AnalyzerDispatcher.cpp
    src/utils/AudioPlayer.cpp
    src/config/Config.cpp
    src/config/Preferences.cpp
    src/config/FilenameConfig.cpp
    src/config/WorkflowManager.cpp
    src/UI_Files.cpp
    src/UI_Actions.cpp
    src/UI_Tasks.cpp
    src/UI_Log.cpp
    src/processors/LoudnessNormalizeProcessor.cpp
    src/processors/TruePeakLimiterProcessor.cpp
    src/processors/DcOffsetRemoveProcessor.cpp
    src/processors/PeakNormalizeProcessor.cpp
    src/pipeline/NodeRegistry.cpp
    src/pipeline/ChainEngine.cpp
    src/pipeline/base_nodes/FileSourceNode.cpp
    src/pipeline/base_nodes/OutputSinkNode.cpp
    src/pipeline/base_nodes/DSPNode.cpp
    src/pipeline/base_nodes/AnalyzerNode.cpp
    src/pipeline/base_nodes/ResamplerNode.cpp
    src/pipeline/base_nodes/NullSinkNode.cpp
    src/pipeline/action_nodes/LoudnessNormalizeNode.cpp
    src/pipeline/action_nodes/TruePeakLimiterNode.cpp
    src/pipeline/action_nodes/LoudnessAnalyzerNode.cpp
    src/pipeline/action_nodes/DcOffsetRemoveNode.cpp
    src/pipeline/action_nodes/PeakNormalizeNode.cpp
    src/pipeline/action_nodes/ChannelMixNode.cpp
    src/pipeline/action_nodes/TrimSilenceNode.cpp
    src/pipeline/action_nodes/FadeNode.cpp
    src/pipeline/action_nodes/LoudnessComplianceNode.cpp
    src/pipeline/action_nodes/ClippingDetectionNode.cpp
    src/pipeline/action_nodes/DynamicRangeNode.cpp
    src/ui_components/Files_MediaPlayer.cpp
    src/ui_components/Files_FileOperations.cpp
    src/ui_components/Actions_Output.cpp
    src/ui_components/Actions_NodeChain.cpp
    src/ui_components/Actions_Lists.cpp
    src/ui_components/Actions_DnD.cpp
    src/ui_components/Actions_Workflows.cpp
    src/ui_components/DnDHandler.cpp
)

# Apply feature switches
if (MANIFOLD_ENABLE_PARALLEL_ANALYZING)
    message (STATUS "${PROJECT_NAME}: Parallel analyzing:\t\tON")
    target_compile_definitions (${PROJECT_NAME} PRIVATE
        ENABLE_PARALLEL_ANALYZING=1
        EBUR128_ANALYZER_THREADS_DEFAULT=${MANIFOLD_EBUR128_ANALYZER_THREADS_DEFAULT}
        DCOFFSET_ANALYZER_THREADS_DEFAULT=${MANIFOLD_DCOFFSET_ANALYZER_THREADS_DEFAULT}
    )
else ()
    message (STATUS "${PROJECT_NAME}: Parallel analyzing:\t\tOFF")
endif ()

#
# Build & Include Dependencies
#

# Dear ImGui
aux_source_directory (deps/imgui SRC_IMGUI)
add_library (imgui STATIC ${SRC_IMGUI})
if (APPLE)
    # Add Metal implementaion file
    target_sources (imgui PRIVATE deps/imgui/imgui_impl_metal.mm)
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

target_link_libraries (${PROJECT_NAME} PRIVATE r8brain_pffft_backend)
target_compile_definitions (${PROJECT_NAME} PRIVATE R8B_PFFFT_DOUBLE=1)

#
# Link Against Libraries
#

# OpenGL 2.x stuff.
# Link -lGL directly instead of CMake's OpenGL::GL / OpenGL::OpenGL targets.
# On Linux with GLVND, CMake's OpenGL discovery can resolve to a library that silently
# breaks Legacy OpenGL2 rendering (black window). Direct -lGL mirrors the official Makefile:
#   LINUX_GL_LIBS = -lGL
# So I only use auto-detected OpenGL libraries on non-Linux platforms.
if (UNIX AND NOT APPLE)
    message (STATUS "Using Linux. Prefer -lGL explicitly for better compatibility.")
    set (GL_LIBRARY -lGL)
elseif (NOT APPLE)
    message (STATUS "Using platform-specific OpenGL2 library detection.")
    find_package (OpenGL REQUIRED)
    set (GL_LIBRARY OpenGL::GL)
else ()
    message (STATUS "Using macOS Metal renderer; OpenGL2 is not linked.")
    set (GL_LIBRARY)
endif ()

# Glfw3
pkg_check_modules (GLFW3 REQUIRED glfw3 IMPORTED_TARGET)
target_include_directories (imgui PRIVATE ${GLFW3_INCLUDE_DIRS})
target_link_libraries (${PROJECT_NAME} PRIVATE ${GL_LIBRARY} PkgConfig::GLFW3 ${CMAKE_DL_LIBS})

if (APPLE)
    # Link against macOS native UI stuff.
    target_link_libraries (${PROJECT_NAME} PRIVATE
        "-framework Metal"
        "-framework QuartzCore"
        "-framework Cocoa"
    )
endif ()

# libsndfile
# Find libsndfile and its dependencies
pkg_check_modules(SNDFILE REQUIRED IMPORTED_TARGET sndfile)
target_include_directories (miniaudio_libsndfile PRIVATE ${SNDFILE_INCLUDE_DIRS})
target_link_libraries (${PROJECT_NAME} PUBLIC PkgConfig::SNDFILE)

# ===========================================================================
# manifold_pipeline_test — Node-based pipeline prototype
# ===========================================================================

set (PIPELINE_SOURCES
    src/pipeline/NodeRegistry.cpp
    src/pipeline/ChainEngine.cpp
    src/pipeline/base_nodes/FileSourceNode.cpp
    src/pipeline/base_nodes/OutputSinkNode.cpp
    src/pipeline/base_nodes/ResamplerNode.cpp
    src/pipeline/base_nodes/DSPNode.cpp    
    src/pipeline/action_nodes/LoudnessAnalyzerNode.cpp
    src/pipeline/action_nodes/PrintInfoNode.cpp
    src/pipeline/action_nodes/LoudnessNormalizeNode.cpp
    src/pipeline/action_nodes/TruePeakLimiterNode.cpp
    src/processors/LoudnessNormalizeProcessor.cpp
    src/processors/TruePeakLimiterProcessor.cpp
    src/utils/LogManager.cpp
)

add_executable (manifold_pipeline_test
    test/pipeline_test.cpp
    ${PIPELINE_SOURCES}
)

target_compile_definitions (manifold_pipeline_test PRIVATE R8B_PFFFT_DOUBLE=1)

target_include_directories (manifold_pipeline_test PRIVATE
    src
    deps/r8brain
    deps/r8brain/fft
    deps/libebur128/ebur128
)

target_link_libraries (manifold_pipeline_test PRIVATE
    r8brain_pffft_backend
    ebur128
    PkgConfig::SNDFILE
    imgui
)

# ===========================================================================
# analyzer_node_test — Functional regression test for AnalyzerNode refactoring
# ===========================================================================

set (ANALYZER_TEST_SOURCES
    src/pipeline/NodeRegistry.cpp
    src/pipeline/ChainEngine.cpp
    src/pipeline/base_nodes/FileSourceNode.cpp
    src/pipeline/base_nodes/NullSinkNode.cpp
    src/pipeline/base_nodes/AnalyzerNode.cpp
    src/pipeline/action_nodes/ClippingDetectionNode.cpp
    src/pipeline/action_nodes/LoudnessComplianceNode.cpp
    src/pipeline/action_nodes/DynamicRangeNode.cpp
    src/utils/LogManager.cpp
)

add_executable (analyzer_node_test
    test/analyzer_node_test.cpp
    ${ANALYZER_TEST_SOURCES}
)

target_include_directories (analyzer_node_test PRIVATE
    src
    deps/libebur128/ebur128
)

target_link_libraries (analyzer_node_test PRIVATE
    ebur128
    PkgConfig::SNDFILE
    imgui
)
