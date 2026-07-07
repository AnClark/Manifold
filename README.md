# Manifold

Manifold is a desktop audio batch-processing tool built for inspecting, transforming, and exporting large collections of audio files with a visual workflow editor. Useful for checking and pre-processing audio for game-audio production, podcasts and broadcast, it is designed to make repetitive tasks more efficient and reproducible.

It combines a polished ImGui-based interface with a custom node-based audio pipeline, allowing you to build repeatable processing chains for tasks such as loudness normalization, peak limiting, DC offset cleanup, resampling, and format conversion.

![Project concept](https://img.shields.io/badge/C%2B%2B-17-blue) ![UI](https://img.shields.io/badge/Interface-ImGui-6c6cff)

## Why Manifold?

Manifold was designed to make audio batch work feel more structured and reproducible. Instead of manually running ad-hoc commands or scripting every step, you can:

- import whole folders of audio files,
- inspect their metadata and analysis results,
- create reusable processing chains,
- export processed files with consistent naming and format settings.

This makes it useful for workflows such as podcast preparation, mastering prep, broadcast delivery, and game-audio asset processing.

## Key Features

- Batch import of files and folders
- Drag-and-drop support from the file explorer
- Built-in audio playback for quick previews
- File table with metadata and analysis metrics
- Configurable processing workflows using nodes
- Output settings for folder, naming, format, and subtype
- Background processing with task history and logs
- Parallel analysis for loudness and DC offset metrics

## Supported Processing Nodes

Manifold includes a growing set of audio processing and analysis nodes, including:

- Loudness analysis
- Loudness normalization
- True peak limiting
- DC offset removal
- Peak normalization
- Channel mixing
- Silence trimming
- Fade in/out
- Loudness compliance checks
- Clipping detection
- Dynamic range analysis

These nodes can be chained together to create custom pipelines for different production scenarios.

## Typical Workflow

1. Add audio files or folders from the Files view.
2. Switch to Actions and build a node chain.
3. Configure output settings such as target folder, naming template, and file format.
4. Run the batch job.
5. Review progress and results in the Tasks and Log views.

## Architecture

The project is organized into a few clear layers:

- UI layer: ImGui windows for files, actions, tasks, and logs
- Pipeline layer: node-based execution engine for processing steps
- Processing layer: DSP implementations and audio processors
- Worker layer: background workers for analysis and file processing
- Configuration layer: preferences, workflow state, and output settings

## Build Instructions

### Prerequisites

You will need:

- CMake
- A C++17 compiler such as GCC, Clang, or MSVC
- pkg-config
- GLFW3
- libsndfile
- OpenGL development libraries

### Build

```bash
cmake -S . -B build
cmake --build build
```

After building, run the application:

- Windows: `build/Manifold.exe`
- Linux/macOS: `build/Manifold`

The build also generates additional test/demo targets such as `manifold_pipeline_test` and `analyzer_node_test`.

## Project Structure

```text
src/
  Main.cpp                 # Application entry point and main UI shell
  UI_Files.cpp             # File management and playback UI
  UI_Actions.cpp           # Workflow builder and output settings UI
  UI_Tasks.cpp             # Batch run history and status UI
  UI_Log.cpp               # Logging and diagnostics UI
  pipeline/                # Node-based processing pipeline
  processors/             # DSP processing implementations
  workers/                 # Background processing workers
  config/                  # Preferences and workflow configuration
  ui_components/           # UI helper components
  utils/                   # Utility modules

test/                      # Regression and prototype tests
```

## License

This project is distributed under the GNU General Public License v3.0. See [LICENSE](LICENSE) for details.

## Status

Manifold is an evolving audio-processing project with a modular architecture and an expanding set of processing capabilities. It is suitable for experimentation, batch audio workflows, and further extension.
