#include "FileSourceNode.hpp"
#include "utils/LogManager.hpp"
#include "utils/SfOpenUtf8.hpp"

#include <stdexcept>

// --------------------------------------------------------------------------
// AudioFileStream
// --------------------------------------------------------------------------

AudioFileStream::AudioFileStream(SNDFILE* sf, const AudioFormat& fmt)
    : sf_(sf), format_(fmt) {}

AudioFileStream::~AudioFileStream()
{
    if (sf_) {
        sf_close(sf_);
        sf_ = nullptr;
    }
}

size_t AudioFileStream::read(float* buffer, size_t frames)
{
    sf_count_t got = sf_readf_float(sf_, buffer, static_cast<sf_count_t>(frames));
    return got > 0 ? static_cast<size_t>(got) : 0;
}

// --------------------------------------------------------------------------
// FileSourceNode
// --------------------------------------------------------------------------

std::unique_ptr<AudioStream> FileSourceNode::create(NodeContext& ctx)
{
    SF_INFO info = {};
    SNDFILE* sf = SfOpenUtf8(ctx.sourcePath.c_str(), SFM_READ, &info);
    if (!sf) {
        LOG_ERRORF("FileSourceNode", "Cannot open '%s': %s", ctx.sourcePath.c_str(), sf_strerror(nullptr));
        throw std::runtime_error(
            "[FileSourceNode] cannot open '" + ctx.sourcePath +
            "': " + sf_strerror(nullptr));
    }

    AudioFormat fmt;
    fmt.sampleRate = static_cast<double>(info.samplerate);
    fmt.channels   = info.channels;

    switch (info.format & SF_FORMAT_SUBMASK) {
        case SF_FORMAT_PCM_S8:   fmt.bitDepth =  8; break;
        case SF_FORMAT_PCM_16:   fmt.bitDepth = 16; break;
        case SF_FORMAT_PCM_24:   fmt.bitDepth = 24; break;
        case SF_FORMAT_PCM_32:   fmt.bitDepth = 32; break;
        case SF_FORMAT_FLOAT:    fmt.bitDepth = 32; break;
        case SF_FORMAT_DOUBLE:   fmt.bitDepth = 64; break;
        case SF_FORMAT_ALAC_16:  fmt.bitDepth = 16; break;
        case SF_FORMAT_ALAC_20:  fmt.bitDepth = 20; break;
        case SF_FORMAT_ALAC_24:  fmt.bitDepth = 24; break;
        case SF_FORMAT_ALAC_32:  fmt.bitDepth = 32; break;
        default:                 fmt.bitDepth = 16; break;
    }

    ctx.sourceFormat = fmt;
    return std::make_unique<AudioFileStream>(sf, fmt);
}
