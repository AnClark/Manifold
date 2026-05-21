#include "AnalyzerNode.hpp"

// --------------------------------------------------------------------------
// AnalyzerStream
// --------------------------------------------------------------------------

AnalyzerStream::AnalyzerStream(std::unique_ptr<AudioStream> upstream, NodeContext& ctx)
    : upstream_(std::move(upstream))
    , ctx_(ctx)
{}

size_t AnalyzerStream::read(float* buf, size_t frames)
{
    size_t got = upstream_->read(buf, frames);
    if (got > 0)
        onSamples(buf, got);
    else if (!finalized_) {
        finalized_ = true;
        onFinalize();
    }
    return got;
}

const AudioFormat& AnalyzerStream::format() const
{
    return upstream_->format();
}
