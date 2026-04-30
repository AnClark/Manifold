#pragma once

#include "base/IAudioProcessor.hpp"

/**
 * @brief Normalizes integrated loudness (LUFS-I) to a target level.
 *
 * This processor applies a fixed linear gain computed from the difference
 * between the measured LUFS-I and the target LUFS-I. A True Peak safety
 * ceiling further clamps the gain to prevent clipping.
 *
 * ### Usage (two-pass workflow)
 * 1. Run EBUR128Worker on the file to obtain lufsI and maxSamplePeak_dBFS.
 * 2. Before processing, inject the measurements:
 *    @code
 *    proc.setParameterValue("measured_lufs",     (float)fileInfo.lufsI);
 *    proc.setParameterValue("measured_peak_dbfs", (float)fileInfo.maxSamplePeak_dBFS);
 *    @endcode
 * 3. Drive the processing chain frame-by-frame as usual.
 *
 * ### Parameters
 * | ID                   | Default | Range       | Description                          |
 * |----------------------|---------|-------------|--------------------------------------|
 * | `target_lufs`        | -16.0   | -36 .. -6   | Target integrated loudness (LUFS-I)  |
 * | `tp_ceiling`         |  -1.0   |  -9 ..  0   | True Peak safety ceiling (dBTP)      |
 * | `measured_lufs`      |   0.0   | -100 ..  0  | Measured LUFS-I (injected at runtime)|
 * | `measured_peak_dbfs` |   0.0   | -100 ..  0  | Measured sample peak dBFS (runtime)  |
 *
 * ### Gain formula
 * @code
 * gain_dB      = target_lufs - measured_lufs
 * max_gain_dB  = tp_ceiling  - measured_peak_dbfs   // headroom guard
 * applied_dB   = min(gain_dB, max_gain_dB)
 * @endcode
 */
class LoudnessNormalizeProcessor : public IAudioProcessor
{
public:
    LoudnessNormalizeProcessor();

    enum ParamIndex
    {
        pTargetLufs = 0,
        pTpCeiling,
        pMeasuredLufs,
        pMeasuredPeakDbfs,
        pParamCount
    };

    const char* getName()        const override { return "Loudness Normalize"; }
    const char* getDescription() const override
    {
        return "Normalizes integrated loudness (LUFS-I) to a target level "
               "with True Peak ceiling protection.";
    }

    const AudioProcessorParam& getParameterDefintion(uint32_t index) const override;

    void process(const float** inputs, float** outputs, int channels, size_t frameCount) override;
};
