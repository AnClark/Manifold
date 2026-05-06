#include "LoudnessNormalizeProcessor.hpp"

#include <algorithm>
#include <cmath>

static constexpr AudioProcessorParam ParamList[] = {
    {"target_lufs", "Target LUFS-I", "Specify target LUFS-I level", -36.0f, -6.0f, -16.0f },
    {"tp_ceiling", "Top Ceiling (dB)", "Specify top ceiling (dB)", -9.0f, 0.0f, -1.0f },
    {"measured_lufs", "Measured LUFS-I", "Specify LUFS-I measured by Manifold.", -100.0f, 0.0f, 0.0f },
    {"measured_peak_dbfs", "Measured Peak dBFS", "", -100.0f, 0.0f, 0.0f },
};

LoudnessNormalizeProcessor::LoudnessNormalizeProcessor() : IAudioProcessor(pParamCount)
{
    for (uint32_t i = 0; i < pParamCount; ++i)
        paramValues[i] = ParamList[i].def;
}

const AudioProcessorParam& LoudnessNormalizeProcessor::getParameterDefintion(uint32_t index) const
{
    if (index >= pParamCount)
        return EmptyParam;
    
    return ParamList[index];
}

void LoudnessNormalizeProcessor::process(
    const float** inputs, float** outputs, int channels, size_t frameCount)
{
    const float& targetLufs     = paramValues[pTargetLufs]; //getParameterValue(pTargetLufs);
    const float& tpCeiling      = paramValues[pTpCeiling];
    const float& measuredLufs   = paramValues[pMeasuredLufs];
    const float& measuredPeakDb = paramValues[pMeasuredPeakDbfs];

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
