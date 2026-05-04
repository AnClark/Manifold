#pragma once

#include "../Node.hpp"

#include <sndfile.h>
#include <string>

/**
 * @brief Sink node that encodes an AudioStream to a file via libsndfile.
 *
 * Parameters (passed via init()):
 *   "format"  — output container/codec: "wav", "flac", "ogg", "opus",
 *               "aiff", "caf", "w64"  (default: "wav")
 *   "subtype" — PCM sub-format override: "pcm16", "pcm24", "pcm32",
 *               "float32"             (default chosen per format)
 *
 * After writing, ctx.currentFilePath is set to the produced file.
 * If ctx.sideband contains "loudness_lufs" (double) it is written as an
 * INFO chunk comment (WAV) or COMMENT tag (FLAC/OGG) where supported.
 */
class OutputSinkNode : public Node, public StreamSinkNode {
public:
    /** Supported output container formats. */
    enum class ContainerFormat  { Wav, Flac, Ogg, Opus, Aiff, Caf, W64 };

    /** PCM sub-format override; Auto means per-format default. */
    enum class SubtypeOverride  { Auto, Pcm16, Pcm24, Pcm32, Float32 };

    std::string name() const override { return "OutputSink"; }
    void init(const std::unordered_map<std::string, std::string>& params) override;

    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::FilePath; }

    void consume(std::unique_ptr<AudioStream> stream, NodeContext& ctx) override;

private:
    ContainerFormat format_  = ContainerFormat::Wav;
    SubtypeOverride subtype_ = SubtypeOverride::Auto;

    /** @return ContainerFormat parsed from a lowercase format string.
     *  @throws std::invalid_argument for unknown format strings. */
    static ContainerFormat parseFormat(const std::string& s);

    /** @return SubtypeOverride parsed from a lowercase subtype string.
     *  @throws std::invalid_argument for unknown subtype strings. */
    static SubtypeOverride parseSubtype(const std::string& s);

    /** @return libsndfile major format constant for the given format. */
    static int majorFormat(ContainerFormat fmt);

    /** @return libsndfile subtype constant. */
    static int subtypeFormat(ContainerFormat fmt, SubtypeOverride subtype);

    /** @return file extension (without leading dot). */
    static std::string extension(ContainerFormat fmt);
};
