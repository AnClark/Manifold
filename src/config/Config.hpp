#pragma once

/**
 * @file Config.hpp
 * @brief Serialisation helpers for persisting configurations to TOML.
 *
 * Two namespaces cover the two independently saveable project artifacts:
 *
 *  - **NodeConfig** — serialises the active node chain (and optionally the
 *    output format settings) to a TOML document.  The resulting string can be
 *    written to a `.toml` preset file and reloaded later.
 *
 *  - **FileConfig** — serialises the current file list to a TOML document,
 *    recording each file's absolute path together with a platform tag so that
 *    path-separator differences can be detected on load.
 *
 * Both namespaces produce self-describing TOML: every root table contains a
 * `config_type` key (see @ref kConfigTypeNodeChain / @ref kConfigTypeFileList)
 * that identifies the document kind for the loader.
 *
 * ### TOML schema — node chain preset
 * @code{.toml}
 * config_type = "node_chain"
 * name        = "My Preset"          # optional
 *
 * [[chain]]
 * id          = "loudness_normalize"  # NodeRegistry factory key
 * target_lufs = "-16.000000"
 * tp_ceiling  = "-1.000000"
 *
 * [[chain]]
 * id   = "channel_mix"
 * mode = "stereo_to_mono"
 *
 * [output]                            # optional — only present when buildOutputConfig() result is passed
 * folder  = "D:/output"
 * format  = "flac"
 * subtype = "pcm24"
 * @endcode
 *
 * ### TOML schema — file list
 * @code{.toml}
 * config_type = "file_list"
 * platform    = "win32"
 *
 * [[file]]
 * path = "C:/Music/track01.wav"
 *
 * [[file]]
 * path = "C:/Music/track02.wav"
 * @endcode
 */

#include "pipeline/Node.hpp"
#include "base/SndFileInfo.hpp"
#include "Preferences.hpp"  // for targetOutputPref

#include <toml.hpp>

#include <string>
#include <vector>
#include <mutex>

// --------------------------------------------------------------------------
// Config-type discriminator constants
// --------------------------------------------------------------------------

/// Value written to the `config_type` key in node-chain TOML documents.
static constexpr const char* kConfigTypeNodeChain = "node_chain";

/// Value written to the `config_type` key in file-list TOML documents.
static constexpr const char* kConfigTypeFileList = "file_list";

// --------------------------------------------------------------------------
// Platform tag constants (written by FileConfig::saveFileList)
// --------------------------------------------------------------------------

/// Platform tag written on Windows (path separator `\\`).
static constexpr const char* kPlatformWin32    = "win32";

/// Platform tag written on POSIX systems (path separator `/`).
static constexpr const char* kPlatformUnixLike = "unix_like";

/// Platform tag written when the separator is unrecognised.
static constexpr const char* kPlatformUnknown  = "unknown";

// --------------------------------------------------------------------------
// NodeConfig — node chain and output-format serialisation
// --------------------------------------------------------------------------

/**
 * @brief Serialisation helpers for the active node chain and output settings.
 */
namespace NodeConfig
{

/**
 * @brief Serialises the node chain to a TOML-formatted string.
 *
 * Iterates over @p nodeChain and calls Node::exportConfig() on each entry to
 * obtain its string-keyed parameters.  Every node is emitted as a `[[chain]]`
 * array-of-tables entry containing at minimum an `id` key (the NodeRegistry
 * factory key used to reconstruct the node) followed by all exported params.
 *
 * @param nodeChain       The ordered list of nodes to serialise.
 * @param nodeChainMutex  Mutex guarding @p targetNodeChain.
 * @param outputConfig    Optional output-format table produced by
 *                        buildOutputConfig().  When non-empty it is embedded
 *                        under the `[output]` key.  Defaults to an empty table
 *                        (no `[output]` section emitted).
 * @param name            Optional human-readable preset name written as the
 *                        top-level `name` key.  Omitted when empty.
 * @return A UTF-8 TOML document as a `std::string`.
 */
std::string saveNodeChain(std::vector<std::shared_ptr<Node>>& nodeChain,
                          std::mutex& nodeChainMutex,
                          toml::table outputConfig = {},
                          std::string name = "");

/**
 * @brief Deserialises a node-chain preset from a TOML file and rebuilds the
 *        node chain in place.
 *
 * Parses the file at @p configFilePath, validates that its `config_type` is
 * `"node_chain"`, then iterates over every `[[chain]]` entry.  For each entry
 * the `id` key is looked up in the NodeRegistry to instantiate the node; the
 * remaining key/value pairs are forwarded to Node::importConfig() so that the
 * node can restore its own parameters.
 *
 * The existing contents of @p targetNodeChain are replaced atomically under
 * @p nodeChainMutex: the mutex is held only for the final swap so that node
 * construction (which may be expensive) happens outside the critical section.
 *
 * When @p targetOutputPref is non-null and the TOML document contains an
 * `[output]` section, the output folder, format and subtype fields are written
 * back into @p *targetOutputPref.  If the section is absent the struct is left
 * unchanged.
 *
 * @param configFilePath    Path to the `.toml` preset file to load.
 * @param targetNodeChain   Node chain to replace with the loaded nodes.
 * @param nodeChainMutex    Mutex guarding @p targetNodeChain.
 * @param targetOutputPref  Optional pointer to an output-format preference
 *                          struct that receives the `[output]` section values.
 *                          Pass `nullptr` to ignore output settings.
 */
void loadNodeChain(std::string_view configFilePath,
                   std::vector<std::shared_ptr<Node>>& targetNodeChain,
                   std::mutex& nodeChainMutex,
                   OutputConfigPref* targetOutputPref = nullptr);

/**
 * @brief Builds a `toml::table` describing the output format settings.
 *
 * The returned table is intended to be passed as the @p outputConfig argument
 * of saveNodeChain().  It contains three keys:
 *
 * | Key       | Example value  | Description                      |
 * |-----------|----------------|----------------------------------|
 * | `folder`  | `"D:/output"`  | Output directory path            |
 * | `format`  | `"flac"`       | Container format name            |
 * | `subtype` | `"pcm24"`      | PCM sub-format / codec name      |
 *
 * @param folder       Absolute path to the output directory.
 * @param formatName   Container format string (e.g. `"wav"`, `"flac"`).
 * @param subTypeName  Sub-format string (e.g. `"pcm16"`, `"float32"`).
 * @return A `toml::table` ready for embedding in a larger TOML document.
 */
toml::table buildOutputConfig(std::string folder,
                              std::string formatName,
                              std::string subTypeName);

}  // namespace NodeConfig

// --------------------------------------------------------------------------
// FileConfig — file list serialisation
// --------------------------------------------------------------------------

/**
 * @brief Serialisation helpers for the active file list.
 */
namespace FileConfig
{

/**
 * @brief Serialises the file list to a TOML-formatted string.
 *
 * Each entry in @p fileList is emitted as a `[[file]]` array-of-tables record
 * containing the absolute `path` of the source file.  A `platform` tag
 * (see @ref getPlatform()) is written at the root level so that a future
 * loader can detect path-separator mismatches between the saving and loading
 * platform.
 *
 * @param fileList  The list of audio files to serialise.
 * @return A UTF-8 TOML document as a `std::string`.
 */
std::string saveFileList(SndFileList& fileList);

/**
 * @brief Returns a string tag identifying the current operating platform.
 *
 * Determined at call time by inspecting `std::filesystem::path::preferred_separator`.
 *
 * @return One of @ref kPlatformWin32, @ref kPlatformUnixLike, or
 *         @ref kPlatformUnknown.
 */
const char* getPlatform();

}  // namespace FileConfig
