#pragma once

#include "../Node.hpp"
#include "base/IAudioProcessor.hpp"

#include <functional>
#include <memory>
#include <string_view>

/**
 * @brief Stream-processor node that delegates sample processing to an IAudioProcessor.
 *
 * The node accepts a factory function so the caller can supply any
 * IAudioProcessor subclass.  A fresh processor instance is created inside
 * every wrap() call, which guarantees that filter / state history never leaks
 * from one file to the next.
 *
 * Audio format conversion:
 *   - Incoming data is interleaved float (from the upstream AudioStream).
 *   - IAudioProcessor::process() expects **planar** (non-interleaved) float.
 *   - DSPStream handles the de-interleave → process → re-interleave round-trip
 *     transparently; IAudioProcessor implementations never need to deal with it.
 */
class DSPNode : public Node, public StreamProcessorNode {
public:
    using ProcessorFactory = std::function<std::unique_ptr<IAudioProcessor>()>;

    /**
     * @param factory   Callable that returns a new IAudioProcessor instance.
     * @param nodeName  Display name shown in engine log messages.
     */
    explicit DSPNode(ProcessorFactory factory, std::string nodeName = "DSP");

    /**
     * @brief Convenience factory: create a DSPNode for a concrete processor type.
     *
     * @tparam T        IAudioProcessor subclass to instantiate.
     * @param  nodeName Display name; defaults to an empty string ("DSP").
     */
    template<typename T>
    static std::unique_ptr<DSPNode> create(std::string nodeName = "DSP") {
        return std::make_unique<DSPNode>(
            []{ return std::make_unique<T>(); },
            std::move(nodeName));
    }

    /**
     * @brief Registry-based factory: look up a processor by its registered string ID.
     *
     * Delegates to ProcessorRegistry::getInstance().create(processorId) inside the
     * factory lambda, so the concrete processor type need not be known at the call site.
     * This is the preferred way to build DSPNodes when the processor set is open-ended
     * (i.e. processors are added via REGISTER_PROCESSOR without touching pipeline code).
     *
     * @param processorId  String ID used in REGISTER_PROCESSOR, e.g. "true_peak_limiter".
     * @param nodeName     Display name; defaults to processorId if empty.
     * @throws std::runtime_error if processorId is not registered.
     */
    static std::unique_ptr<DSPNode> fromRegistry(std::string_view processorId,
                                                  std::string nodeName = "");

    std::string name() const override { return nodeName_; }
    void init(const std::unordered_map<std::string, std::string>& params) override;

    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::AudioStream; }

    std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) override;

private:
    ProcessorFactory factory_;
    std::string      nodeName_;

    // Configuration params forwarded to the processor via setParameterValue
    std::unordered_map<std::string, std::string> initParams_;
};
