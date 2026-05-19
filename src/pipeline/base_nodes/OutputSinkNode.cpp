#include "OutputSinkNode.hpp"
#include "utils/SfOpenUtf8.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <iomanip>
#include <sstream>
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

    // NOTE: AudioFormat and AudioFormats are different symbols with different usages.
    //       See: src/base/AudioFormats.hpp
    const int sfMajor     = AudioFormats::majorFormat(format_);
    const int sfSubtype   = AudioFormats::subtypeFormat(format_, subtype_);
    const std::string ext = AudioFormats::extension(format_);

    // Determine the output stem: use ctx.outputStem when the caller has pre-resolved
    // a template, otherwise fall back to the source file's stem (no "_out" suffix).
    std::string stem;
    if (!ctx.outputStem.empty())
        stem = ctx.outputStem;
    else
        // Default: original stem without any added suffix
        stem = std::filesystem::u8path(ctx.sourcePath).stem().u8string();

    // Ensure output directory exists before resolving conflicts / opening the file.
    std::filesystem::create_directories(std::filesystem::u8path(ctx.outputDir));

    // Compute tentative output path and handle conflicts.
    auto makeOutPath = [&](const std::string& s) {
        return ctx.outputDir + "/" + s + "." + ext;
    };

    std::string outPath = makeOutPath(stem);

    switch (ctx.outputConflictPolicy)
    {
        case NodeContext::OutputConflictPolicy::Skip:
            if (std::filesystem::exists(std::filesystem::u8path(outPath)))
            {
                ctx.currentFilePath = outPath;  // record path as-is; nothing written
                return;
            }
            break;

        case NodeContext::OutputConflictPolicy::Overwrite:
            break;  // proceed, sf_open will truncate the existing file

        case NodeContext::OutputConflictPolicy::AutoRename:
        {
            int suffix = 1;
            while (std::filesystem::exists(std::filesystem::u8path(outPath)))
            {
                std::ostringstream oss;
                oss << stem << "_" << std::setw(2) << std::setfill('0') << suffix++;
                outPath = makeOutPath(oss.str());
            }
            break;
        }
    }

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

    bool cancelled = false;
    size_t got;
    while ((got = stream->read(buf.data(), kBlockFrames)) > 0)
    {
        if (ctx.isCancelled()) { cancelled = true; break; }
        sf_writef_float(outSf, buf.data(), static_cast<sf_count_t>(got));
    }

    if (cancelled)
    {
        sf_close(outSf);
        // Remove the partially-written output file to avoid leaving corrupt data on disk.
        std::error_code ec;
        std::filesystem::remove(std::filesystem::u8path(outPath), ec);
        return;  // caller (SingleFileProcessorWorker) checks isCancelled() to set record status
    }

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
