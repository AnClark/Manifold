#include "ResamplerNode.hpp"
#include "pipeline/NodeRegistry.hpp"

// r8brain headers — included only in this translation unit
#include "CDSPResampler.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>
#include <vector>

REGISTER_NODE(
    ResamplerNode,
    "resampler",
    "Resampler",
    "Converts the audio stream to a target sample rate using a high-quality linear-phase resampler.",
    "Format",
    NodeRole::StreamProcessor);

// ============================================================================
// R8BrainBackend::Impl
// ============================================================================

struct R8BrainBackend::Impl {
    std::vector<std::unique_ptr<r8b::CDSPResampler24>> resamplers;
    // Per-channel scratch buffers (doubles)
    std::vector<std::vector<double>> inBufs;
    std::vector<std::vector<double>> outBufs;  // holds per-channel output per call
};

// ============================================================================
// R8BrainBackend
// ============================================================================

R8BrainBackend::R8BrainBackend()
    : impl_(std::make_unique<Impl>()) {}

R8BrainBackend::~R8BrainBackend() = default;

void R8BrainBackend::init(double srcRate, double dstRate, int channels)
{
    srcRate_  = srcRate;
    dstRate_  = dstRate;
    channels_ = channels;

    impl_->resamplers.clear();
    impl_->inBufs.clear();
    impl_->outBufs.clear();

    impl_->inBufs.resize(channels, std::vector<double>(kMaxInLen));
    impl_->outBufs.resize(channels);

    impl_->resamplers.reserve(channels);
    for (int ch = 0; ch < channels; ++ch) {
        impl_->resamplers.push_back(
            std::make_unique<r8b::CDSPResampler24>(srcRate, dstRate, kMaxInLen));
    }
}

void R8BrainBackend::processInterleaved(const float* in, size_t inFrames,
                                         std::vector<float>& out)
{
    assert(channels_ > 0);
    assert(inFrames <= static_cast<size_t>(kMaxInLen));

    // De-interleave and convert float → double
    for (int ch = 0; ch < channels_; ++ch) {
        impl_->inBufs[ch].resize(inFrames);
        for (size_t i = 0; i < inFrames; ++i) {
            impl_->inBufs[ch][i] = static_cast<double>(in[i * channels_ + ch]);
        }
    }

    // Process each channel through its own resampler
    int outCount = 0;
    for (int ch = 0; ch < channels_; ++ch) {
        double* outPtr = nullptr;
        int n = impl_->resamplers[ch]->process(
            impl_->inBufs[ch].data(),
            static_cast<int>(inFrames),
            outPtr);

        outCount = n;  // same for all channels at same input length
        impl_->outBufs[ch].assign(outPtr, outPtr + n);
    }

    if (outCount <= 0) return;

    // Interleave and convert double → float
    size_t base = out.size();
    out.resize(base + static_cast<size_t>(outCount) * channels_);
    for (int i = 0; i < outCount; ++i) {
        for (int ch = 0; ch < channels_; ++ch) {
            out[base + i * channels_ + ch] = static_cast<float>(impl_->outBufs[ch][i]);
        }
    }
}

void R8BrainBackend::process(const float* in, size_t inFrames,
                               std::vector<float>& out)
{
    if (channels_ == 0 || inFrames == 0) return;

    // Process in kMaxInLen-sized chunks to respect the resampler's MaxInLen
    size_t offset = 0;
    while (offset < inFrames) {
        size_t chunk = std::min(inFrames - offset,
                                static_cast<size_t>(kMaxInLen));
        processInterleaved(in + offset * channels_, chunk, out);
        offset += chunk;
    }
}

void R8BrainBackend::flush(std::vector<float>& out)
{
    if (channels_ == 0) return;

    // Feed kMaxInLen zeros to drain the filter's tail
    std::vector<float> zeros(kMaxInLen * channels_, 0.0f);
    process(zeros.data(), static_cast<size_t>(kMaxInLen), out);
}

// ============================================================================
// LibSampleRateBackend (stub)
// ============================================================================

void LibSampleRateBackend::init(double /*srcRate*/, double /*dstRate*/, int /*channels*/)
{
    throw std::logic_error("LibSampleRateBackend is not yet implemented");
}

void LibSampleRateBackend::process(const float* /*in*/, size_t /*inFrames*/,
                                    std::vector<float>& /*out*/)
{
    throw std::logic_error("LibSampleRateBackend is not yet implemented");
}

void LibSampleRateBackend::flush(std::vector<float>& /*out*/)
{
    throw std::logic_error("LibSampleRateBackend is not yet implemented");
}

// ============================================================================
// ResamplerStream
// ============================================================================

namespace {

/**
 * @brief Wraps an upstream stream with pull-based resampling.
 *
 * Internally maintains an output FIFO (interleaved float) to absorb the
 * non-uniform output block sizes produced by the resampler (especially on
 * the first several blocks during the pre-roll / latency phase).
 */
class ResamplerStream : public AudioStream {
public:
    ResamplerStream(std::unique_ptr<AudioStream>     upstream,
                    std::unique_ptr<ResamplerBackend> backend,
                    double                            dstRate)
        : upstream_(std::move(upstream))
        , backend_(std::move(backend))
    {
        const AudioFormat& upFmt = upstream_->format();
        format_ = upFmt;
        format_.sampleRate = dstRate;

        backend_->init(upFmt.sampleRate, dstRate, upFmt.channels);
    }

    size_t read(float* buffer, size_t frames) override
    {
        // Fill FIFO until we have enough frames or upstream is exhausted
        while (availableFrames() < frames && !fullyDrained()) {
            pullMore();
        }

        size_t avail    = availableFrames();
        size_t toReturn = std::min(frames, avail);
        if (toReturn == 0) return 0;

        size_t samples = toReturn * static_cast<size_t>(format_.channels);
        std::copy(fifo_.begin() + fifoReadPos_,
                  fifo_.begin() + fifoReadPos_ + static_cast<ptrdiff_t>(samples),
                  buffer);
        fifoReadPos_ += samples;
        compactFifo();
        return toReturn;
    }

    const AudioFormat& format() const override { return format_; }

private:
    std::unique_ptr<AudioStream>     upstream_;
    std::unique_ptr<ResamplerBackend> backend_;
    AudioFormat                       format_;

    std::vector<float> fifo_;
    size_t             fifoReadPos_ = 0;

    bool upstreamEof_ = false;
    bool flushed_     = false;

    static constexpr size_t kPullFrames = 1024;

    bool fullyDrained() const { return upstreamEof_ && flushed_; }

    size_t availableFrames() const
    {
        return (fifo_.size() - fifoReadPos_) /
               static_cast<size_t>(format_.channels);
    }

    void pullMore()
    {
        if (upstreamEof_ && flushed_) return;

        if (!upstreamEof_) {
            std::vector<float> inBuf(kPullFrames *
                                     static_cast<size_t>(format_.channels));
            size_t got = upstream_->read(inBuf.data(), kPullFrames);
            if (got > 0) {
                backend_->process(inBuf.data(), got, fifo_);
                return;
            }
            upstreamEof_ = true;
        }

        if (!flushed_) {
            backend_->flush(fifo_);
            flushed_ = true;
        }
    }

    void compactFifo()
    {
        // Compact when the dead zone is more than half the buffer
        if (fifoReadPos_ > fifo_.size() / 2) {
            fifo_.erase(fifo_.begin(),
                        fifo_.begin() + static_cast<ptrdiff_t>(fifoReadPos_));
            fifoReadPos_ = 0;
        }
    }
};

}  // anonymous namespace

// ============================================================================
// ResamplerNode
// ============================================================================

void ResamplerNode::init(const std::unordered_map<std::string, std::string>& params)
{
    auto it = params.find("rate");
    if (it != params.end()) {
        targetRate_ = std::stod(it->second);
        if (targetRate_ <= 0)
            throw std::invalid_argument("ResamplerNode: 'rate' must be positive");
    }

    it = params.find("backend");
    if (it != params.end()) backendId_ = it->second;
}

std::unique_ptr<ResamplerBackend> ResamplerNode::makeBackend() const
{
    if (backendId_ == "r8brain" || backendId_.empty())
        return std::make_unique<R8BrainBackend>();
    if (backendId_ == "libsamplerate")
        return std::make_unique<LibSampleRateBackend>();
    throw std::invalid_argument("ResamplerNode: unknown backend '" + backendId_ + "'");
}

std::unique_ptr<AudioStream> ResamplerNode::wrap(
    std::unique_ptr<AudioStream> upstream,
    NodeContext& /*ctx*/)
{
    const double srcRate = upstream->format().sampleRate;

    // Passthrough: no resampling needed
    if (std::abs(srcRate - targetRate_) < 0.5)
        return upstream;

    return std::make_unique<ResamplerStream>(
        std::move(upstream), makeBackend(), targetRate_);
}
