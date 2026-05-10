#include "OutputSinkNode.hpp"
#include "utils/SfOpenUtf8.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

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
