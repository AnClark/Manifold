#pragma once

#include "../Node.hpp"

#include <sndfile.h>

/**
 * @brief Lazily-read audio stream backed by libsndfile.
 *
 * Frames are pulled on demand via sf_readf_float. The SNDFILE handle is
 * closed in the destructor, ensuring no resource leaks even if the stream
 * is abandoned mid-way.
 */
class AudioFileStream : public AudioStream {
public:
    AudioFileStream(SNDFILE* sf, const AudioFormat& fmt);
    ~AudioFileStream() override;

    size_t read(float* buffer, size_t frames) override;
    const AudioFormat& format() const override { return format_; }

private:
    SNDFILE*    sf_;
    AudioFormat format_;
};

// --------------------------------------------------------------------------

/**
 * @brief Source node that opens an audio file via libsndfile.
 *
 * The path is taken from ctx.sourcePath. ctx.sourceFormat is populated during
 * create() so that downstream nodes (e.g. ResamplerNode) can read it.
 *
 * Supported formats: anything libsndfile can read (WAV, AIFF, FLAC, OGG,
 * CAF, W64, etc.).
 */
class FileSourceNode : public Node, public SourceNode {
public:
    std::string name() const override { return "FileSource"; }
    void init(const std::unordered_map<std::string, std::string>& /*params*/) override {}

    PortType primaryInput()  const override { return PortType::None; }
    PortType primaryOutput() const override { return PortType::AudioStream; }

    std::unique_ptr<AudioStream> create(NodeContext& ctx) override;
};
