#include "LoudnessNormalizeProcessor.hpp"
#include "base/ProcessorRegistry.hpp"

#include <algorithm>
#include <cmath>

REGISTER_PROCESSOR(LoudnessNormalizeProcessor, "loudness_normalize")

LoudnessNormalizeProcessor::LoudnessNormalizeProcessor()
{
    //                          id                    default   min     max
    addParameter("target_lufs",        -16.0f,  -36.0f,   -6.0f);
    addParameter("tp_ceiling",          -1.0f,   -9.0f,    0.0f);
    addParameter("measured_lufs",        0.0f, -100.0f,    0.0f);   // Passed in fileInfo.lufsI
    addParameter("measured_peak_dbfs",   0.0f, -100.0f,    0.0f);   // Passed in fileInfo.maxSamplePeak_dBFS
}

void LoudnessNormalizeProcessor::process(
    const float** inputs, float** outputs, int channels, size_t frameCount)
{
    const float targetLufs     = getParameterValue("target_lufs");
    const float tpCeiling      = getParameterValue("tp_ceiling");
    const float measuredLufs   = getParameterValue("measured_lufs");
    const float measuredPeakDb = getParameterValue("measured_peak_dbfs");

    // Gain required to hit target loudness
    float gainDb = targetLufs - measuredLufs;

    // Clamp so that the sample peak does not exceed the TP ceiling after gain
    const float maxAllowedGainDb = tpCeiling - measuredPeakDb;
    gainDb = std::min(gainDb, maxAllowedGainDb);

    const float gainLinear = std::pow(10.0f, gainDb / 20.0f);

    for (int ch = 0; ch < channels; ++ch)
        for (size_t i = 0; i < frameCount; ++i)
            outputs[ch][i] = inputs[ch][i] * gainLinear;
}
