#include "OutputSinkNode.hpp"
#include "utils/SfOpenUtf8.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// --------------------------------------------------------------------------
// Format resolution helpers
// --------------------------------------------------------------------------

OutputSinkNode::ContainerFormat OutputSinkNode::parseFormat(const std::string& s)
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

OutputSinkNode::SubtypeOverride OutputSinkNode::parseSubtype(const std::string& s)
{
    if (s.empty() || s == "auto") return SubtypeOverride::Auto;
    if (s == "pcm16")   return SubtypeOverride::Pcm16;
    if (s == "pcm24")   return SubtypeOverride::Pcm24;
    if (s == "pcm32")   return SubtypeOverride::Pcm32;
    if (s == "float32") return SubtypeOverride::Float32;
    throw std::invalid_argument("OutputSinkNode: unknown subtype '" + s + "'");
}

int OutputSinkNode::majorFormat(ContainerFormat fmt)
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

int OutputSinkNode::subtypeFormat(ContainerFormat fmt, SubtypeOverride subtype)
{
    // Explicit override takes precedence
    switch (subtype) {
        case SubtypeOverride::Pcm16:   return SF_FORMAT_PCM_16;
        case SubtypeOverride::Pcm24:   return SF_FORMAT_PCM_24;
        case SubtypeOverride::Pcm32:   return SF_FORMAT_PCM_32;
        case SubtypeOverride::Float32: return SF_FORMAT_FLOAT;
        case SubtypeOverride::Auto:    break;
    }

    // Per-format defaults
    switch (fmt) {
        case ContainerFormat::Flac: return SF_FORMAT_PCM_24;
        case ContainerFormat::Ogg:  return SF_FORMAT_VORBIS;
#ifdef SF_FORMAT_OPUS
        case ContainerFormat::Opus: return SF_FORMAT_OPUS;
#endif
        case ContainerFormat::Caf:  return SF_FORMAT_PCM_24;
        default:                    return SF_FORMAT_PCM_16;  // WAV, AIFF, W64 default to 16-bit
    }
}

std::string OutputSinkNode::extension(ContainerFormat fmt)
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

// --------------------------------------------------------------------------
// Node implementation
// --------------------------------------------------------------------------

void OutputSinkNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("format");
    if (it != params.end()) {
        std::string s = it->second;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        format_ = parseFormat(s);
    }

    it = params.find("subtype");
    if (it != params.end()) {
        std::string s = it->second;
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        subtype_ = parseSubtype(s);
    }
}

void OutputSinkNode::consume(std::unique_ptr<AudioStream> stream, NodeContext& ctx)
{
    const AudioFormat& fmt = stream->format();

    int sfMajor   = majorFormat(format_);
    int sfSubtype = subtypeFormat(format_, subtype_);
    std::string ext = extension(format_);

    // Build output path: <outputDir>/<stem>_out.<ext>
    // TODO: Allow applying user's own wildcard
    // u8string() ensures the stem is returned as UTF-8 on all platforms
    // (path::string() would return the current ANSI code page on Windows).
    std::string outPath =
        ctx.outputDir + "/" + (std::filesystem::u8path(ctx.sourcePath).stem().u8string() + "_out." + ext);

    // Ensure output directory exists
    std::filesystem::create_directories(std::filesystem::u8path(ctx.outputDir));

    SF_INFO outInfo   = {};
    outInfo.samplerate = static_cast<int>(fmt.sampleRate);
    outInfo.channels   = fmt.channels;
    outInfo.format     = sfMajor | sfSubtype;

    if (!sf_format_check(&outInfo)) {
        throw std::runtime_error(
            "OutputSinkNode: invalid libsndfile format combination (format enum=" +
            std::to_string(static_cast<int>(format_)) + ")");
    }

    SNDFILE* outSf = SfOpenUtf8(outPath.c_str(), SFM_WRITE, &outInfo);
    if (!outSf) {
        throw std::runtime_error(
            "OutputSinkNode: cannot open output '" + outPath +
            "': " + sf_strerror(nullptr));
    }

    // Drive the pull loop
    constexpr size_t kBlockFrames = 4096;
    std::vector<float> buf(kBlockFrames * static_cast<size_t>(fmt.channels));

    size_t got;
    while ((got = stream->read(buf.data(), kBlockFrames)) > 0) {
        sf_writef_float(outSf, buf.data(), static_cast<sf_count_t>(got));
    }
    // stream goes out of scope after consume() returns → triggers loudness finalisation

    // Optional: write loudness metadata if available
    auto loudness = ctx.getSideband<double>("loudness_lufs");
    if (loudness.has_value()) {
        std::string comment = "integrated_loudness=" +
                              std::to_string(*loudness) + " LUFS";
        sf_set_string(outSf, SF_STR_COMMENT, comment.c_str());
    }

    sf_close(outSf);
    ctx.currentFilePath = outPath;
}
