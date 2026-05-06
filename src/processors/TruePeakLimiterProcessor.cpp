#include "TruePeakLimiterProcessor.hpp"

// Use the PFFFT double-precision FFT backend.
// pffft_double.c is compiled into r8brain_pffft_backend and linked to this binary.
#define R8B_PFFFT_DOUBLE 1
#include "CDSPResampler.h"

#include <algorithm>
#include <cmath>
#include <vector>


// ---------------------------------------------------------------------------

static constexpr AudioProcessorParam ParamList[] = {
    {"tp_ceiling",             "True Peak Ceiling (dBTP)", "Specify True Peak ceiling (dBTP)",    -9.0f,   0.0f,  -1.0f },
    {"measured_true_peak_dbtp", "Measured True Peak", "Measured True Peak (injected at runtime)", -100.0f, 0.0f,  0.0f },
};

TruePeakLimiterProcessor::TruePeakLimiterProcessor() : IAudioProcessor(pParamCount)
{
    for (uint32_t i = 0; i < pParamCount; ++i)
        paramValues[i] = ParamList[i].def;
}

const AudioProcessorParam& TruePeakLimiterProcessor::getParameterDefintion(uint32_t index) const
{
    if (index >= pParamCount)
        return EmptyParam;

    return ParamList[index];
}

void TruePeakLimiterProcessor::process(
    const float** inputs, float** outputs, int channels, size_t frameCount)
{
    // NOTE: This method is NOT called by TruePeakLimiterNode.
    // That node overrides DSPNode::wrap() entirely and implements a self-contained
    // two-pass strategy: it buffers the full stream, calls the static
    // measureTruePeakDbtp(), then replays with a pre-computed scalar gain.
    // This method exists for standalone / programmatic use of the processor
    // (e.g. unit tests or future pipeline variants that inject "measured_true_peak_dbtp"
    // via sideband instead of buffering).

    const float& tpCeiling  = paramValues[pTpCeiling];
    const float& measuredTp = paramValues[pMeasuredTruePeakDbtp];

    // Only attenuate — if the signal is already below the ceiling, this is a pass-through.
    const float gainDb     = std::min(0.0f, tpCeiling - measuredTp);
    const float gainLinear = std::pow(10.0f, gainDb / 20.0f);

    for (int ch = 0; ch < channels; ++ch)
        for (size_t i = 0; i < frameCount; ++i)
            outputs[ch][i] = inputs[ch][i] * gainLinear;
}

// ---------------------------------------------------------------------------

double TruePeakLimiterProcessor::measureTruePeakDbtp(
    const float** channels, int numChannels, size_t frameCount)
{
    if (!channels || numChannels <= 0 || frameCount == 0)
        return -144.0;

    // kChunk: number of input samples per process() call.
    // Must not exceed the aMaxInLen passed to CDSPResampler24's constructor.
    static constexpr int kChunk = 512;

    double maxTruePeak = 0.0;
    std::vector<double> inBuf(kChunk, 0.0);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        if (!channels[ch])
            continue;

        // CDSPResampler24: linear-phase, 24-bit quality, 4× upsampling.
        // Using ratio 1.0 → 4.0 rather than absolute sample rates, as the True
        // Peak value is independent of the actual sample rate.
        r8b::CDSPResampler24 resampler(1.0, 4.0, kChunk);

        // Linear-phase filters have symmetric group delay.  After feeding all
        // real samples, we must push getInLenBeforeOutPos(0) additional zero
        // samples to flush the filter tail and recover the delayed output.
        const int flushLen = resampler.getInLenBeforeOutPos(0);

        // ---- Phase 1: process real signal in kChunk-sized pieces -----------
        size_t pos = 0;
        while (pos < frameCount)
        {
            const int toProcess = (int)std::min((size_t)kChunk, frameCount - pos);

            for (int i = 0; i < toProcess; ++i)
                inBuf[i] = (double)channels[ch][pos + i];

            pos += toProcess;

            double* outPtr = nullptr;
            const int outCount = resampler.process(inBuf.data(), toProcess, outPtr);

            for (int i = 0; i < outCount; ++i)
                maxTruePeak = std::max(maxTruePeak, std::abs(outPtr[i]));
        }

        // ---- Phase 2: flush filter tail with zero-padding ------------------
        std::fill(inBuf.begin(), inBuf.end(), 0.0);
        int flushed = 0;
        while (flushed < flushLen)
        {
            const int toFlush = std::min(kChunk, flushLen - flushed);
            flushed += toFlush;

            double* outPtr = nullptr;
            const int outCount = resampler.process(inBuf.data(), toFlush, outPtr);

            for (int i = 0; i < outCount; ++i)
                maxTruePeak = std::max(maxTruePeak, std::abs(outPtr[i]));
        }
    }

    if (maxTruePeak <= 0.0)
        return -144.0;

    return 20.0 * std::log10(maxTruePeak);
}
