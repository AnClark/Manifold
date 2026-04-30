#pragma once

#include <vector>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>

enum AudioProcessorParamType
{
    kParamFloat,
    kParamInt,
    kParamEnum
};

struct AudioProcessorParam
{
    const char* id = "";
    const char* displayName = "";
    const char* description = "";
    float min = 0.0f;
    float max = 1.0f;
    float def = 0.0f;
    AudioProcessorParamType type = kParamFloat;
};
static AudioProcessorParam EmptyParam;  // For fallback usage (e.g. Invalid param index)

struct AudioProcessorEnumItem
{
    const char* displayText;
    float value;
};

class IAudioProcessor {
public:
    IAudioProcessor(uint32_t paramCount) : paramCount(paramCount)
    {
        // Resize (not reserve) so that size() == paramCount immediately.
        // Subclass constructors are then responsible for filling in default values.
        paramValues.resize(paramCount, 0.0f);
    }
    virtual ~IAudioProcessor() = default;

    // --------------------------------------------------------------------------
    // Metadata

    virtual const char* getName() const = 0;
    virtual const char* getDescription() const = 0;

    // --------------------------------------------------------------------------
    // Parameters

    uint32_t getParameterCount() { return paramCount; }

    virtual const AudioProcessorParam& getParameterDefintion(uint32_t index) const
    {
        return EmptyParam;
    };

    float getParameterValue(uint32_t index) const
    {
        if (index >= paramCount || index >= paramValues.size() )
            return 0.0f;
        
        return paramValues[index];
    }

    void setParameterValue(uint32_t index, float value)
    {
        if (index >= paramCount || index >= paramValues.size() )
            return;

        paramValues[index] = value;
        onParameterChanged(index, value);

        // If parameter not found, do nothing.
        // TODO: Consider logging a warning or throwing an exception in log.
    }

    void setParameterValue(const char* id, float value)
    {
        for (uint32_t index = 0; index < paramCount; index++)
        {
            const AudioProcessorParam& currentParam = getParameterDefintion(index);
            if (std::strcmp(currentParam.id, id) == 0)
            {
                setParameterValue(index, value);
                return;
            }
        }
    }

    // --------------------------------------------------------------------------
    // Event callbacks (optional overrides)

    virtual void onParameterChanged(uint32_t index, float newValue) {}
    virtual void onProcessStart() {}    // TODO: This is preserved. Not ready to implement this yet.
    virtual void onProcessEnd() {}      // TODO: This is preserved. Not ready to implement this yet.

    // --------------------------------------------------------------------------
    // Processing

    virtual void process(const float** inputs, float** outputs, int channels, size_t frameCount) = 0;

protected:
    // --------------------------------------------------------------------------
    // Internal data (visible in subclasses)

    std::vector<float> paramValues;
    uint32_t paramCount;
};
