#pragma once

#include <string>
#include <stdexcept>

#include <sndfile.h>

/**
 * @brief Supported audio format definitions for Manifold.
 *
 * Provides the container format and subtype enumerations used throughout
 * the program (including OutputSinkNode, UI, and pipeline nodes), together
 * with a set of stateless helper functions that resolve those enumerations
 * to libsndfile constants, file-extension strings, or enum values parsed
 * from human-readable strings.
 * 
 * @note This module is only for audio file output.
 * Audio input format is handled in SndFileInfo.
 * 
 * @warning Not to be confused with @ref AudioFormat in @file pipeline/AudioStream.cpp.
 * 
 * @see OutputSinkNode, SndFileInfo
 */
namespace AudioFormats
{

/**
 * @brief Supported output container formats.
 *
 * Each enumerator maps to one top-level libsndfile major format
 * (see @ref majorFormat()).  Opus reuses the OGG container at the
 * libsndfile level but uses a distinct enumerator so that the correct
 * codec subtype (@c SF_FORMAT_OPUS) can be selected automatically.
 */
enum class ContainerFormat
{
    Wav,   ///< PCM Wave (.wav)
    Flac,  ///< Free Lossless Audio Codec (.flac)
    Ogg,   ///< Ogg Vorbis (.ogg)
    Opus,  ///< Opus codec in an Ogg container (.ogg)
    Aiff,  ///< Audio Interchange File Format (.aiff)
    Caf,   ///< Apple Core Audio Format (.caf)
    W64,   ///< Sony Wave64 (.w64)
};

/**
 * @brief PCM sub-format selection.
 *
 * When set to @c Auto the per-container default chosen by
 * @ref subtypeFormat() is used.  Any other value unconditionally
 * overrides that default (subject to per-container clamping for
 * formats that do not support all subtypes).
 */
enum class SubtypeOverride
{
    Auto,     ///< Use the per-container default subtype
    Pcm16,    ///< 16-bit signed integer PCM  (@c SF_FORMAT_PCM_16)
    Pcm24,    ///< 24-bit signed integer PCM  (@c SF_FORMAT_PCM_24)
    Pcm32,    ///< 32-bit signed integer PCM  (@c SF_FORMAT_PCM_32)
    Float32,  ///< 32-bit IEEE-754 float       (@c SF_FORMAT_FLOAT)
    Double64, ///< 64-bit IEEE-754 double      (@c SF_FORMAT_DOUBLE)
};

// --------------------------------------------------------------------------
// Format resolution helpers
// --------------------------------------------------------------------------

/**
 * @brief Parse a lowercase format string into a @ref ContainerFormat.
 *
 * Accepted tokens: @c "wav", @c "flac", @c "ogg", @c "opus",
 * @c "aiff", @c "caf", @c "w64".
 *
 * @param s  Lowercase format identifier (e.g. @c "flac").
 * @return   The corresponding @ref ContainerFormat enumerator.
 * @throws   std::invalid_argument if @p s is not a recognised token.
 */
static ContainerFormat parseFormat(const std::string& s)
{
    if (s == "wav")  return ContainerFormat::Wav;
    if (s == "flac") return ContainerFormat::Flac;
    if (s == "ogg")  return ContainerFormat::Ogg;
    if (s == "opus") return ContainerFormat::Opus;
    if (s == "aiff") return ContainerFormat::Aiff;
    if (s == "caf")  return ContainerFormat::Caf;
    if (s == "w64")  return ContainerFormat::W64;
    throw std::invalid_argument("OutputSinkNode: unknown format '" + s + "'");
}

/**
 * @brief Parse a lowercase subtype string into a @ref SubtypeOverride.
 *
 * Accepted tokens: @c "" (empty string), @c "auto", @c "pcm16",
 * @c "pcm24", @c "pcm32", @c "float32", @c "double64".
 * Both an empty string and @c "auto" map to @ref SubtypeOverride::Auto.
 *
 * @param s  Lowercase subtype identifier (e.g. @c "pcm24"), or an empty
 *           string to request the per-format default.
 * @return   The corresponding @ref SubtypeOverride enumerator.
 * @throws   std::invalid_argument if @p s is not a recognised token.
 */
static SubtypeOverride parseSubtype(const std::string& s)
{
    if (s.empty() || s == "auto") return SubtypeOverride::Auto;
    if (s == "pcm16")    return SubtypeOverride::Pcm16;
    if (s == "pcm24")    return SubtypeOverride::Pcm24;
    if (s == "pcm32")    return SubtypeOverride::Pcm32;
    if (s == "float32")  return SubtypeOverride::Float32;
    if (s == "double64") return SubtypeOverride::Double64;
    throw std::invalid_argument("OutputSinkNode: unknown subtype '" + s + "'");
}

/**
 * @brief Return the libsndfile major-format constant for a container format.
 *
 * The returned value is suitable for use as the upper bits of the
 * @c SF_INFO::format field.  Both @ref ContainerFormat::Ogg and
 * @ref ContainerFormat::Opus map to @c SF_FORMAT_OGG because Opus
 * uses the same container; the codec distinction is handled by the
 * subtype (see @ref subtypeFormat()).
 *
 * @param fmt  The desired output container format.
 * @return     A libsndfile @c SF_FORMAT_* major-format constant.
 */
static int majorFormat(ContainerFormat fmt)
{
    switch (fmt) {
        case ContainerFormat::Wav:  return SF_FORMAT_WAV;
        case ContainerFormat::Flac: return SF_FORMAT_FLAC;
        case ContainerFormat::Ogg:  return SF_FORMAT_OGG;
        case ContainerFormat::Opus: return SF_FORMAT_OGG;   // OGG container, Opus codec
        case ContainerFormat::Aiff: return SF_FORMAT_AIFF;
        case ContainerFormat::Caf:  return SF_FORMAT_CAF;
        case ContainerFormat::W64:  return SF_FORMAT_W64;
    }
    return SF_FORMAT_WAV;  // unreachable
}

/**
 * @brief Return the libsndfile subtype constant for a format/subtype pair.
 *
 * Codec-locked formats always return a fixed subtype regardless of @p subtype:
 *   - @c Ogg  → @c SF_FORMAT_VORBIS  (codec is always Vorbis)
 *   - @c Opus → @c SF_FORMAT_OPUS    (or @c SF_FORMAT_VORBIS as fallback when
 *               @c SF_FORMAT_OPUS is not defined by the installed libsndfile)
 *
 * FLAC only supports 16- and 24-bit integer PCM; @c Pcm32, @c Float32, and
 * @c Double64 overrides are silently clamped to @c SF_FORMAT_PCM_24.
 *
 * For all remaining formats (WAV, AIFF, CAF, W64) an explicit @p subtype
 * override takes precedence over the per-container default.  When @p subtype
 * is @ref SubtypeOverride::Auto the per-container default is used:
 *   - @c Caf  → @c SF_FORMAT_PCM_24
 *   - @c Wav / @c Aiff / @c W64 → @c SF_FORMAT_PCM_16
 *
 * @param fmt      The target container format.
 * @param subtype  The requested subtype, or @ref SubtypeOverride::Auto
 *                 to apply the per-format default.
 * @return         A libsndfile @c SF_FORMAT_* subtype constant.
 */
static int subtypeFormat(ContainerFormat fmt, SubtypeOverride subtype)
{
    // Codec-locked formats: subtype is always determined by the codec;
    // user overrides are meaningless and must be ignored.
    if (fmt == ContainerFormat::Ogg)  return SF_FORMAT_VORBIS;
#ifdef SF_FORMAT_OPUS
    if (fmt == ContainerFormat::Opus) return SF_FORMAT_OPUS;
#else
    if (fmt == ContainerFormat::Opus) return SF_FORMAT_VORBIS;  // libsndfile too old for Opus
#endif

    // FLAC only supports PCM_16 and PCM_24; clamp anything else to PCM_24.
    if (fmt == ContainerFormat::Flac) {
        if (subtype == SubtypeOverride::Pcm16) return SF_FORMAT_PCM_16;
        return SF_FORMAT_PCM_24;
    }

    // WAV, AIFF, CAF, W64: honour explicit override.
    switch (subtype) {
        case SubtypeOverride::Pcm16:    return SF_FORMAT_PCM_16;
        case SubtypeOverride::Pcm24:    return SF_FORMAT_PCM_24;
        case SubtypeOverride::Pcm32:    return SF_FORMAT_PCM_32;
        case SubtypeOverride::Float32:  return SF_FORMAT_FLOAT;
        case SubtypeOverride::Double64: return SF_FORMAT_DOUBLE;
        case SubtypeOverride::Auto:     break;
    }

    // Per-format Auto defaults.
    if (fmt == ContainerFormat::Caf) return SF_FORMAT_PCM_24;
    return SF_FORMAT_PCM_16;  // WAV, AIFF, W64 default to 16-bit
}

/**
 * @brief Return the recommended file-name extension for a container format.
 *
 * The returned string does **not** include a leading dot.
 * @ref ContainerFormat::Opus returns @c "ogg" because Opus streams are
 * stored inside an Ogg container.
 *
 * @param fmt  The target container format.
 * @return     Lowercase extension string, e.g. @c "wav", @c "flac",
 *             @c "ogg", @c "aiff", @c "caf", or @c "w64".
 */
static std::string extension(ContainerFormat fmt)
{
    switch (fmt) {
        case ContainerFormat::Opus: return "ogg";   // Opus uses .ogg container extension
        case ContainerFormat::Flac: return "flac";
        case ContainerFormat::Ogg:  return "ogg";
        case ContainerFormat::Aiff: return "aiff";
        case ContainerFormat::Caf:  return "caf";
        case ContainerFormat::W64:  return "w64";
        default:                    return "wav";
    }
}

/**
 * @brief Return the canonical format name string for a container format.
 *
 * This is the inverse of @ref parseFormat(): the returned string is
 * guaranteed to be accepted by @ref parseFormat() and to round-trip
 * back to the same enumerator.
 *
 * @param fmt  The target container format.
 * @return     Lowercase format name, e.g. @c "wav", @c "flac", @c "ogg",
 *             @c "opus", @c "aiff", @c "caf", or @c "w64".
 */
static std::string formatName(ContainerFormat fmt)
{
    switch (fmt) {
        case ContainerFormat::Wav:  return "wav";
        case ContainerFormat::Flac: return "flac";
        case ContainerFormat::Ogg:  return "ogg";
        case ContainerFormat::Opus: return "opus";
        case ContainerFormat::Aiff: return "aiff";
        case ContainerFormat::Caf:  return "caf";
        case ContainerFormat::W64:  return "w64";
    }
    return "wav";  // unreachable
}

/**
 * @brief Return the canonical subtype name string for a subtype override.
 *
 * This is the inverse of @ref parseSubtype(): the returned string is
 * guaranteed to be accepted by @ref parseSubtype() and to round-trip
 * back to the same enumerator.  @ref SubtypeOverride::Auto returns
 * @c "auto" (equivalent to passing an empty string to @ref parseSubtype()).
 *
 * @param subtype  The subtype override enumerator.
 * @return         Lowercase subtype name: @c "auto", @c "pcm16",
 *                 @c "pcm24", @c "pcm32", @c "float32", or @c "double64".
 */
static std::string subtypeName(SubtypeOverride subtype)
{
    switch (subtype) {
        case SubtypeOverride::Auto:     return "auto";
        case SubtypeOverride::Pcm16:    return "pcm16";
        case SubtypeOverride::Pcm24:    return "pcm24";
        case SubtypeOverride::Pcm32:    return "pcm32";
        case SubtypeOverride::Float32:  return "float32";
        case SubtypeOverride::Double64: return "double64";
    }
    return "auto";  // unreachable
}

} // namespace AudioFormats
