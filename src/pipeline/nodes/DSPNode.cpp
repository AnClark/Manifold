#include "DSPNode.hpp"
#include "base/ProcessorRegistry.hpp"

#include <algorithm>
#include <cstdlib>
#include <vector>

// ============================================================================
// DSPStream — internal stream that applies an IAudioProcessor
// ============================================================================

namespace {

class DSPStream : public AudioStream {
public:
    DSPStream(std::unique_ptr<AudioStream>    upstream,
              std::unique_ptr<IAudioProcessor> processor)
        : upstream_(std::move(upstream))
        , processor_(std::move(processor))
    {
        const int ch = upstream_->format().channels;
        planarIn_.resize(ch);
        planarOut_.resize(ch);
        inputPtrs_.resize(ch);
        outputPtrs_.resize(ch);
    }

    size_t read(float* buffer, size_t frames) override
    {
        const AudioFormat& fmt = upstream_->format();
        const int ch = fmt.channels;

        // Resize scratch buffers if needed
        if (scratchSize_ < frames) {
            scratchSize_ = frames;
            for (int c = 0; c < ch; ++c) {
                planarIn_[c].resize(frames);
                planarOut_[c].resize(frames);
            }
        }

        // Pull interleaved data from upstream
        size_t got = upstream_->read(buffer, frames);
        if (got == 0) return 0;

        // De-interleave: interleaved float → planar float
        for (size_t i = 0; i < got; ++i) {
            for (int c = 0; c < ch; ++c) {
                planarIn_[c][i] = buffer[i * ch + c];
            }
        }

        // Set up pointer arrays for IAudioProcessor
        for (int c = 0; c < ch; ++c) {
            inputPtrs_[c]  = planarIn_[c].data();
            outputPtrs_[c] = planarOut_[c].data();
        }

        processor_->process(
            const_cast<const float**>(inputPtrs_.data()),
            outputPtrs_.data(),
            ch,
            got);

        // Re-interleave: planar float → interleaved float (back into buffer)
        for (size_t i = 0; i < got; ++i) {
            for (int c = 0; c < ch; ++c) {
                buffer[i * ch + c] = planarOut_[c][i];
            }
        }

        return got;
    }

    const AudioFormat& format() const override { return upstream_->format(); }

private:
    std::unique_ptr<AudioStream>     upstream_;
    std::unique_ptr<IAudioProcessor> processor_;

    // Planar scratch buffers ("planar" = non-interleaved format)
    std::vector<std::vector<float>> planarIn_;
    std::vector<std::vector<float>> planarOut_;
    std::vector<float*>             inputPtrs_;
    std::vector<float*>             outputPtrs_;
    size_t                          scratchSize_ = 0;
};

}  // anonymous namespace

// ============================================================================
// DSPNode
// ============================================================================

DSPNode::DSPNode(ProcessorFactory factory, std::string nodeName)
    : factory_(std::move(factory))
    , nodeName_(std::move(nodeName))
{}

std::unique_ptr<DSPNode> DSPNode::fromRegistry(std::string_view processorId,
                                                std::string nodeName)
{
    std::string id(processorId);
    if (nodeName.empty()) nodeName = id;
    return std::make_unique<DSPNode>(
        [id]{ return ProcessorRegistry::getInstance().create(id); },
        std::move(nodeName));
}

void DSPNode::init(const std::unordered_map<std::string, std::string>& params)
{
    initParams_ = params;
}

std::unique_ptr<AudioStream> DSPNode::wrap(
    std::unique_ptr<AudioStream> upstream,
    NodeContext& /*ctx*/)
{
    auto processor = factory_();

    // Apply numeric parameters supplied via init()
    for (const auto& [key, value] : initParams_) {
        try {
            float v = std::stof(value);
            processor->setParameterValue(key.c_str(), v);
        } catch (...) {
            // Non-numeric params are silently ignored here
        }
    }

    return std::make_unique<DSPStream>(std::move(upstream), std::move(processor));
}
