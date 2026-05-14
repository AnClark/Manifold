#include "NullSinkNode.hpp"

#include <vector>

void NullSinkNode::consume(std::unique_ptr<AudioStream> stream, NodeContext& /*ctx*/)
{
    // Drain the stream to trigger EOF-based sideband writes (e.g. LoudnessAnalyzerNode).
    // Audio data is intentionally discarded; no file is written.
    constexpr size_t kBlockFrames = 4096;
    const size_t channels = static_cast<size_t>(stream->format().channels);
    std::vector<float> buf(kBlockFrames * channels);

    while (stream->read(buf.data(), kBlockFrames) > 0) {}
}
