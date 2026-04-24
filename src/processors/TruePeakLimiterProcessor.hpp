#pragma once

#include "base/IAudioProcessor.hpp"

/**
 * @brief Limits True Peak (TP) to a ceiling using 4× oversampled peak detection.
 *
 * ### What is True Peak?
 * ITU-R BS.1770-4 defines True Peak as the maximum absolute value of a signal
 * when reconstructed at an infinitely high sample rate.  A signal that appears
 * within bounds at its native sample rate can exhibit inter-sample peaks (ISPs)
 * that exceed 0 dBFS after D/A conversion.  Detecting these requires oversampled
 * analysis; this processor uses 4× oversampling via r8brain CDSPResampler24.
 *
 * ### Two-pass workflow (batch processing)
 * 1. Call measureTruePeakDbtp() on the audio data, typically after any preceding
 *    gain stages such as LoudnessNormalizeProcessor.
 * 2. Inject the result before streaming:
 *    @code
 *    proc.setParameterValue("measured_true_peak_dbtp", (float)tp);
 *    @endcode
 * 3. Drive the processing chain frame-by-frame as usual.
 *
 * The processor only attenuates (applied gain ≤ 0 dB); it never boosts.
 * If the measured TP is already at or below the ceiling, process() is a pass-through.
 *
 * ### Parameters
 * | ID                        | Default | Range      | Description                 |
 * |---------------------------|---------|------------|-----------------------------|
 * | `tp_ceiling`              |  -1.0   |  -9 ..  0  | Target True Peak (dBTP)     |
 * | `measured_true_peak_dbtp` |   0.0   | -100 ..  0 | Measured TP (injected)      |
 */
class TruePeakLimiterProcessor : public IAudioProcessor
{
public:
    TruePeakLimiterProcessor();

    const char* getName()        const override { return "True Peak Limiter"; }
    const char* getDescription() const override
    {
        return "Attenuates the signal so that the 4x oversampled True Peak "
               "does not exceed the configured ceiling (dBTP).";
    }

    void process(const float** inputs, float** outputs, int channels, size_t frameCount) override;

    /**
     * @brief Measures the True Peak of an audio buffer using 4× oversampling.
     *
     * Each channel is independently upsampled via r8brain CDSPResampler24
     * (linear-phase, 24-bit quality, PFFFT double-precision FFT backend).
     * The function reports the highest absolute sample across all channels
     * and all oversampled positions, converted to dBTP.
     *
     * A linear-phase filter introduces group delay; the filter tail is flushed
     * by appending getInLenBeforeOutPos(0) zero samples after the real signal,
     * so no tail energy is lost.
     *
     * @param channels    Array of per-channel float sample buffers (non-interleaved).
     * @param numChannels Number of channels.
     * @param frameCount  Number of frames (samples per channel).
     * @return True Peak in dBTP.  Returns -144.0 for silent or empty input.
     */
    static double measureTruePeakDbtp(const float** channels, int numChannels,
                                      size_t frameCount);
};
