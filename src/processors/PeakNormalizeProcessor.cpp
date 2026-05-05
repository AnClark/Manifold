#include "PeakNormalizeProcessor.hpp"

#include <cmath>

static constexpr AudioProcessorParam ParamList[] = {
    {"target_dbfs",        "Target Peak (dBFS)",    "Target sample peak level in dBFS",         -36.0f, 0.0f, -1.0f},
    {"measured_peak_dbfs", "Measured Peak (dBFS)",  "Measured sample peak (injected at runtime)", -144.0f, 0.0f, -1.0f},
};

PeakNormalizeProcessor::PeakNormalizeProcessor() : IAudioProcessor(pParamCount)
{
    for (uint32_t i = 0; i < pParamCount; ++i)
        paramValues[i] = ParamList[i].def;
}

const AudioProcessorParam& PeakNormalizeProcessor::getParameterDefintion(uint32_t index) const
{
    if (index >= pParamCount)
        return EmptyParam;
    return ParamList[index];
}

void PeakNormalizeProcessor::process(
    const float** inputs, float** outputs, int channels, size_t frameCount)
{
    const float gainDb     = paramValues[pTargetDbfs] - paramValues[pMeasuredPeakDbfs];
    const float gainLinear = std::pow(10.0f, gainDb / 20.0f);

    for (int ch = 0; ch < channels; ++ch)
        for (size_t i = 0; i < frameCount; ++i)
            outputs[ch][i] = inputs[ch][i] * gainLinear;
}
