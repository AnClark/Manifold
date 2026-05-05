#include "DcOffsetRemoveProcessor.hpp"

DcOffsetRemoveProcessor::DcOffsetRemoveProcessor() : IAudioProcessor(0)
{}

void DcOffsetRemoveProcessor::setOffsets(const std::vector<float>& offsets)
{
    offsets_ = offsets;
}

void DcOffsetRemoveProcessor::process(
    const float** inputs, float** outputs, int channels, size_t frameCount)
{
    for (int ch = 0; ch < channels; ++ch) {
        const float offset = (ch < static_cast<int>(offsets_.size())) ? offsets_[ch] : 0.0f;
        for (size_t i = 0; i < frameCount; ++i)
            outputs[ch][i] = inputs[ch][i] - offset;
    }
}
