#include "LoudnessAnalyzerNode.hpp"
#include "pipeline/NodeRegistry.hpp"

#include "utils/LogManager.hpp"

#include <ebur128.h>
#include <cmath>
#include <limits>
#include <vector>


// ============================================================================
// LoudnessAnalyzerStream
// ============================================================================

namespace {

class LoudnessAnalyzerStream : public AnalyzerStream {
public:
    LoudnessAnalyzerStream(std::unique_ptr<AudioStream> upstream, NodeContext& ctx)
        : AnalyzerStream(std::move(upstream), ctx)
        , ebur128State_(nullptr)
    {
        const AudioFormat& fmt = format();

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

protected:
    void onSamples(const float* buf, size_t frames) override
    {
        // Feed interleaved float samples to libebur128
        if (ebur128State_) {
            ebur128_add_frames_float(ebur128State_, buf, frames);
        }
    }

    void onFinalize() override
    {
        // EOF reached — compute and store results in the sideband
        if (!ebur128State_) return;

        double lufs = -std::numeric_limits<double>::infinity();
        if (ebur128_loudness_global(ebur128State_, &lufs) == EBUR128_SUCCESS) {
            ctx_.setSideband("loudness_lufs", lufs);
        }

        // Collect max sample peak across all channels
        const AudioFormat& fmt = format();
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

private:
    ebur128_state* ebur128State_;
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
