#pragma once

#include <vector>
#include <string>
#include <vector>

struct AudioProcessorParam
{
    std::string id;
    float value;
    float min;
    float max;
    float def;
};
typedef std::vector<AudioProcessorParam> AudioProcessorParamList;

class IAudioProcessor {
public:
    virtual ~IAudioProcessor() = default;

    // --------------------------------------------------------------------------
    // Metadata

    virtual const char* getName() const = 0;
    virtual const char* getDescription() const = 0;

    // --------------------------------------------------------------------------
    // Parameters

    void addParameter(const char* id, float defaultValue, float minValue, float maxValue)
    {
        _paramList.push_back({id, defaultValue, minValue, maxValue, defaultValue});
    }

    const AudioProcessorParam& getParameterData(const char* id) const 
    {
        // Find parameter by id
        for (const auto& param : _paramList)
        {
            if (param.id == id)
                return param;
        }

        // Return empty param if not found
        static AudioProcessorParam emptyParam = {"", 0.0f, 0.0f, 0.0f, 0.0f};
        return emptyParam;
    }

    float getParameterValue(const char* id) const
    {
        return getParameterData(id).value;
    }

    void setParameterValue(const char* id, float value)
    {
        for (auto& param : _paramList)
        {
            if (param.id == id)
            {
                param.value = value;
                onParameterChanged(id, value);
                return;
            }
        }

        // If parameter not found, do nothing.
        // TODO: Consider logging a warning or throwing an exception in log.
    }

    // --------------------------------------------------------------------------
    // Event callbacks (optional overrides)

    virtual void onParameterChanged(const char* id, float newValue) {}
    virtual void onProcessStart() {}    // TODO: This is preserved. Not ready to implement this yet.
    virtual void onProcessEnd() {}      // TODO: This is preserved. Not ready to implement this yet.

    // --------------------------------------------------------------------------
    // Processing

    virtual void process(const float** inputs, float** outputs, int channels, size_t frameCount) = 0;

private:
    AudioProcessorParamList _paramList;
};
