#pragma once

#include "base/IAudioProcessor.hpp"

/**
 * @brief Normalizes sample peak to a target level (dBFS).
 *
 * Applies a fixed linear gain computed from the difference between the
 * measured sample peak and the target dBFS.  Gain may be positive (boost)
 * or negative (cut) — unlike TruePeakLimiterProcessor, this processor does
 * not clamp direction.
 *
 * ### Usage (two-pass workflow)
 * 1. Run EBUR128Worker on the file to obtain maxSamplePeak_dBFS.
 * 2. Before processing, inject the measurement:
 *    @code
 *    proc.setParameterValue("measured_peak_dbfs", (float)fileInfo.maxSamplePeak_dBFS);
 *    @endcode
 * 3. Drive the processing chain frame-by-frame as usual.
 *
 * ### Default behaviour
 * Both `target_dbfs` and `measured_peak_dbfs` default to -1.0, so
 * gain_dB = 0 when no measurement is injected — a transparent pass-through.
 *
 * ### Parameters
 * | ID                   | Default | Range       | Description                           |
 * |----------------------|---------|-------------|---------------------------------------|
 * | `target_dbfs`        |  -1.0   | -36 ..  0   | Target sample peak (dBFS)             |
 * | `measured_peak_dbfs` |  -1.0   | -144 ..  0  | Measured sample peak (injected)       |
 *
 * ### Gain formula
 * @code
 * gain_dB     = target_dbfs - measured_peak_dbfs
 * gainLinear  = 10 ^ (gain_dB / 20)
 * @endcode
 */
class PeakNormalizeProcessor : public IAudioProcessor
{
public:
    PeakNormalizeProcessor();

    enum ParamIndex
    {
        pTargetDbfs = 0,
        pMeasuredPeakDbfs,
        pParamCount
    };

    const char* getName()        const override { return "Peak Normalize"; }
    const char* getDescription() const override
    {
        return "Normalizes the sample peak to a target dBFS level.";
    }

    const AudioProcessorParam& getParameterDefintion(uint32_t index) const override;

    void process(const float** inputs, float** outputs, int channels, size_t frameCount) override;
};
