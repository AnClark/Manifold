#pragma once

#include "base/IAudioProcessor.hpp"

#include <vector>

/**
 * @brief Subtracts a fixed per-channel DC offset from every sample.
 *
 * DC offset is the average sample value of an audio signal (a constant bias
 * away from zero). It is inaudible on its own but consumes headroom, degrades
 * the precision of subsequent dynamic processors, and can cause audible clicks
 * at edit boundaries.  Removing it before any gain stage is standard practice.
 *
 * ### Usage (two-pass workflow)
 * 1. Run DcOffsetWorker on the file to obtain per-channel offsets.
 * 2. Before processing, inject the measurements:
 *    @code
 *    std::vector<float> offsets = ...;  // from DcOffsetWorker results
 *    proc.setOffsets(offsets);
 *    @endcode
 * 3. Drive the processing chain frame-by-frame as usual.
 *
 * ### Behaviour
 * - If setOffsets() has not been called (or was called with an empty vector),
 *   process() is a transparent pass-through.
 * - If offsets.size() < channels, extra channels are treated as offset = 0.
 * - If offsets.size() > channels, extra values are silently ignored.
 *
 * ### Parameters
 * This processor has no standard IAudioProcessor parameters.  Per-channel
 * offsets are injected via setOffsets() because the channel count is unknown
 * at construction time and cannot be encoded as a fixed parameter list.
 */
class DcOffsetRemoveProcessor : public IAudioProcessor
{
public:
    DcOffsetRemoveProcessor();

    const char* getName()        const override { return "DC Offset Remove"; }
    const char* getDescription() const override
    {
        return "Subtracts the measured per-channel DC offset from the audio signal.";
    }

    void process(const float** inputs, float** outputs, int channels, size_t frameCount) override;

    /**
     * @brief Inject per-channel DC offsets to subtract during streaming.
     *
     * Typically called from DcOffsetRemoveNode::configureProcessorFromNodeContext()
     * after reading the "dc_offsets" sideband key populated by ChainEngine from
     * DcOffsetWorker results.
     *
     * @param offsets  Per-channel DC offset values (linear sample units, e.g. 0.003f).
     */
    void setOffsets(const std::vector<float>& offsets);

private:
    std::vector<float> offsets_;
};
