#include "LoudnessAnalyzerNode.hpp"
#include "pipeline/NodeRegistry.hpp"

#include "utils/LogManager.hpp"

#include <cmath>
#include <vector>


// ============================================================================
// LoudnessAnalyzerStream
// ============================================================================

namespace {

class LoudnessAnalyzerStream : public AudioStream {
public:
    LoudnessAnalyzerStream(std::unique_ptr<AudioStream> upstream, NodeContext& ctx)
        : upstream_(std::move(upstream))
        , ctx_(ctx)
        , ebur128State_(nullptr)
    {
        const AudioFormat& fmt = upstream_->format();

        ebur128State_ = ebur128_init(
            static_cast<unsigned int>(fmt.channels),
            static_cast<unsigned long>(fmt.sampleRate),
            EBUR128_MODE_I | EBUR128_MODE_SAMPLE_PEAK);

        if (!ebur128State_) {
            LOG_FATAL("LoudnessAnalyzer", "ebur128_init failed — loudness will not be measured");
        }
    }

    ~LoudnessAnalyzerStream() override
    {
        if (ebur128State_) {
            ebur128_destroy(&ebur128State_);
        }
    }

    size_t read(float* buffer, size_t frames) override
    {
        size_t got = upstream_->read(buffer, frames);

        if (got > 0) {
            // Feed interleaved float samples to libebur128
            if (ebur128State_) {
                ebur128_add_frames_float(ebur128State_, buffer,
                                         static_cast<size_t>(got));
            }
        } else if (!finalized_) {
            // EOF reached — compute and store results
            finalize();
        }

        return got;
    }

    const AudioFormat& format() const override { return upstream_->format(); }

private:
    std::unique_ptr<AudioStream> upstream_;
    NodeContext&                 ctx_;
    ebur128_state*               ebur128State_;
    bool                         finalized_ = false;

    void finalize()
    {
        finalized_ = true;
        if (!ebur128State_) return;

        double lufs = -std::numeric_limits<double>::infinity();
        if (ebur128_loudness_global(ebur128State_, &lufs) == EBUR128_SUCCESS) {
            ctx_.setSideband("loudness_lufs", lufs);
        }

        // Collect max sample peak across all channels
        const AudioFormat& fmt = upstream_->format();
        double maxPeak = 0.0;
        for (int ch = 0; ch < fmt.channels; ++ch) {
            double peak = 0.0;
            if (ebur128_sample_peak(ebur128State_,
                                    static_cast<unsigned int>(ch), &peak) ==
                EBUR128_SUCCESS) {
                maxPeak = std::max(maxPeak, peak);
            }
        }

        if (maxPeak > 0.0) {
            double peakDbfs = 20.0 * std::log10(maxPeak);
            ctx_.setSideband("loudness_peak_dbfs", peakDbfs);
        }
    }
};

}  // anonymous namespace

// ============================================================================
// LoudnessAnalyzerNode
// ============================================================================

std::unique_ptr<AudioStream> LoudnessAnalyzerNode::wrap(
    std::unique_ptr<AudioStream> upstream,
    NodeContext& ctx)
{
    return std::make_unique<LoudnessAnalyzerStream>(std::move(upstream), ctx);
}
