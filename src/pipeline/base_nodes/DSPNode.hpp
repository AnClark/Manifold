#pragma once

#include "../Node.hpp"
#include "base/IAudioProcessor.hpp"

#include <functional>
#include <memory>

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
 *
 * Subclassing:
 *   Override configureProcessor() to inject per-file runtime values (e.g. from
 *   NodeContext::sideband) into the processor before samples are pulled.
 *   The base implementation is a no-op; static parameters set via init() are
 *   always applied before configureProcessor() is called.
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
     * @brief Convenience helper: returns a factory that constructs processor type T.
     * Function name is inspired by std::make_unique() and std::make_shared().
     *
     * Eliminates the boilerplate lambda in every DSPNode subclass constructor:
     * @code
     * // Before:
     * MyNode::MyNode() : DSPNode([]{ return std::make_unique<MyProcessor>(); }, "My") {}
     * // After:
     * MyNode::MyNode() : DSPNode(DSPNode::makeProcessor<MyProcessor>(), "My") {}
     * @endcode
     */
    template<typename T>
    static ProcessorFactory makeProcessor() {
        return [] { return std::make_unique<T>(); };
    }

    std::string name() const override { return nodeName_; }
    void init(const std::unordered_map<std::string, std::string>& /*params*/) override {}

    std::string nodeHint() const override { return "DSP"; }

    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::AudioStream; }

    std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) override;

    /**
     * @brief For UI: Get parameter definitions from IAudioProcessor instance.
     * 
     * When adding a node, UI can directly invoke this method to get parameter list dynamically,
     * without knowing what processor is. This makes Manifold extensible if I want to add
     * new DSPNodes/Audio Processors in future (no need to hard-code metadata).
     * 
     * Currently, fetchProcessorParamList() creates a temporary instance to fetch parameter list
     * via IAudioProcessor::getParameterDefintion().
     * 
     * Developers can override this default implementation.
     */
    virtual void fetchProcessorParamList(std::vector<AudioProcessorParam>& output)
    {
        auto tmpProcessor = factory_();
        output.clear();

        for (int index = 0; index < tmpProcessor->getParameterCount(); index++)
        {
            output.push_back(tmpProcessor->getParameterDefintion(index));
        }
    }

protected:
    /**
     * @brief Hook called after static init() params are applied, before streaming begins.
     *
     * Override in subclasses to read per-file data from NodeContext::sideband and
     * inject it into the processor via proc.setParameterValue().
     * The default implementation does nothing.
     */
    virtual void configureProcessorFromNodeContext(IAudioProcessor& proc, NodeContext& ctx);

private:
    ProcessorFactory factory_;
    std::string      nodeName_;
};
