#pragma once

#include "ProcessorRegistry.hpp"

class ProcessingChain {
public:
    // 从配置（如 JSON/命令行参数）按 id 构建链
    void add(std::string_view processorId) {
        _chain.push_back(ProcessorRegistry::getInstance().create(processorId));
    }

    void process(const float** inputs, float** outputs, int channels, size_t frameCount) {
        for (auto& p : _chain) {
            p->process(inputs, outputs, channels, frameCount);
        }
    }

private:
    std::vector<std::unique_ptr<IAudioProcessor>> _chain;
};
