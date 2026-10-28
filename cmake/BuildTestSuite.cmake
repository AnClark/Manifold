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
    src/pipeline/base_nodes/AnalyzerNode.cpp
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

# ===========================================================================
# Filename template stem-conflict pre-check (pure logic, no audio I/O).
# ===========================================================================

add_executable (stem_conflict_test
    test/stem_conflict_test.cpp
    src/config/FilenameConfig.cpp
)

target_include_directories (stem_conflict_test PRIVATE
    src
    deps/toml
)

target_link_libraries (stem_conflict_test PRIVATE
    PkgConfig::SNDFILE
)

# ===========================================================================
# Processing dispatcher / pipeline behaviour integration test.
# ===========================================================================

add_executable (processing_dispatch_test
    test/processing_dispatch_test.cpp
    src/base/Worker.cpp
    src/base/SndFileInfo.cpp
    src/workers/SingleFileProcessorWorker.cpp
    src/dispatchers/ProcessingDispatcher.cpp
    src/pipeline/ChainEngine.cpp
    src/pipeline/NodeRegistry.cpp
    src/pipeline/base_nodes/FileSourceNode.cpp
    src/pipeline/base_nodes/OutputSinkNode.cpp
    src/pipeline/base_nodes/NullSinkNode.cpp
    src/pipeline/base_nodes/AnalyzerNode.cpp
    src/pipeline/base_nodes/DSPNode.cpp
    src/pipeline/action_nodes/TruePeakLimiterNode.cpp
    src/pipeline/action_nodes/TrimSilenceNode.cpp
    src/processors/TruePeakLimiterProcessor.cpp
    src/utils/LogManager.cpp
)

target_compile_definitions (processing_dispatch_test PRIVATE R8B_PFFFT_DOUBLE=1)

target_include_directories (processing_dispatch_test PRIVATE
    src
    deps/toml
    deps/r8brain
    deps/r8brain/fft
    deps/imgui
)

target_link_libraries (processing_dispatch_test PRIVATE
    r8brain_pffft_backend
    PkgConfig::SNDFILE
    imgui
)
