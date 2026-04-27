#include "OutputSinkNode.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

// --------------------------------------------------------------------------
// Format resolution helpers
// --------------------------------------------------------------------------

int OutputSinkNode::majorFormat(const std::string& fmt)
{
    if (fmt == "wav")  return SF_FORMAT_WAV;
    if (fmt == "flac") return SF_FORMAT_FLAC;
    if (fmt == "ogg")  return SF_FORMAT_OGG;
    if (fmt == "opus") return SF_FORMAT_OGG;   // OGG container, Opus codec
    if (fmt == "aiff") return SF_FORMAT_AIFF;
    if (fmt == "caf")  return SF_FORMAT_CAF;
    if (fmt == "w64")  return SF_FORMAT_W64;
    return SF_FORMAT_WAV;  // fallback
}

int OutputSinkNode::subtypeFormat(const std::string& fmt,
                                   const std::string& subtypeOverride)
{
    // Explicit override takes precedence
    if (!subtypeOverride.empty()) {
        if (subtypeOverride == "pcm16")   return SF_FORMAT_PCM_16;
        if (subtypeOverride == "pcm24")   return SF_FORMAT_PCM_24;
        if (subtypeOverride == "pcm32")   return SF_FORMAT_PCM_32;
        if (subtypeOverride == "float32") return SF_FORMAT_FLOAT;
    }

    // Per-format defaults
    if (fmt == "flac") return SF_FORMAT_PCM_24;
    if (fmt == "ogg")  return SF_FORMAT_VORBIS;
#ifdef SF_FORMAT_OPUS
    if (fmt == "opus") return SF_FORMAT_OPUS;
#endif
    if (fmt == "caf")  return SF_FORMAT_PCM_24;
    return SF_FORMAT_PCM_16;  // WAV, AIFF, W64 default to 16-bit
}

std::string OutputSinkNode::extension(const std::string& fmt)
{
    if (fmt == "opus") return "ogg";  // Opus uses .ogg container extension
    if (fmt == "flac") return "flac";
    if (fmt == "ogg")  return "ogg";
    if (fmt == "aiff") return "aiff";
    if (fmt == "caf")  return "caf";
    if (fmt == "w64")  return "w64";
    return "wav";
}

// --------------------------------------------------------------------------
// Node implementation
// --------------------------------------------------------------------------

void OutputSinkNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("format");
    if (it != params.end()) {
        formatStr_ = it->second;
        std::transform(formatStr_.begin(), formatStr_.end(),
                       formatStr_.begin(), ::tolower);
    }

    it = params.find("subtype");
    if (it != params.end()) {
        subtypeStr_ = it->second;
        std::transform(subtypeStr_.begin(), subtypeStr_.end(),
                       subtypeStr_.begin(), ::tolower);
    }
}

void OutputSinkNode::consume(std::unique_ptr<AudioStream> stream, NodeContext& ctx)
{
    const AudioFormat& fmt = stream->format();

    int sfMajor   = majorFormat(formatStr_);
    int sfSubtype = subtypeFormat(formatStr_, subtypeStr_);
    std::string ext = extension(formatStr_);

    // Build output path: <outputDir>/<stem>_out.<ext>
    std::filesystem::path outPath =
        ctx.outputDir / (ctx.sourcePath.stem().string() + "_out." + ext);

    // Ensure output directory exists
    std::filesystem::create_directories(ctx.outputDir);

    SF_INFO outInfo   = {};
    outInfo.samplerate = static_cast<int>(fmt.sampleRate);
    outInfo.channels   = fmt.channels;
    outInfo.format     = sfMajor | sfSubtype;

    if (!sf_format_check(&outInfo)) {
        throw std::runtime_error(
            "OutputSinkNode: invalid libsndfile format combination for '" +
            formatStr_ + "'");
    }

    SNDFILE* outSf = sf_open(outPath.c_str(), SFM_WRITE, &outInfo);
    if (!outSf) {
        throw std::runtime_error(
            "OutputSinkNode: cannot open output '" + outPath.string() +
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
