#pragma once

#include "../Node.hpp"

#include <memory>

// --------------------------------------------------------------------------
// AnalyzerStream
// --------------------------------------------------------------------------

/**
 * @brief Base class for analysis stream decorators used by AnalyzerNode subclasses.
 *
 * Encapsulates the pass-through skeleton shared by all audio analyzers via the
 * Template Method pattern:
 *   - Stores upstream_ (the wrapped stream) and ctx_ (per-file context).
 *   - read() forwards data to the Sink unchanged, then fires two hooks:
 *       onSamples()  — invoked for every non-empty buffer read from upstream.
 *       onFinalize() — invoked exactly once when upstream reaches EOF.
 *   - format() delegates to upstream_.
 *
 * ## Subclassing
 *
 * Override the two protected hooks:
 *   - onSamples(buf, frames): accumulate inline statistics from the live stream.
 *     The default implementation is a no-op, suitable for Offline-only analyzers
 *     that do not observe the live signal at all.
 *   - onFinalize(): build the Report from accumulated state and emit it via
 *     ctx_.onReport().  May also run an independent Offline scan here.
 *     Note: the base class has already set the internal finalized_ flag before
 *     calling onFinalize(), so no re-entry guard is needed inside the override.
 *
 * ## Lifecycle
 *
 *   (Sink calls read() repeatedly)
 *   → read() calls upstream_->read()
 *   → onSamples() is fired while upstream returns frames > 0
 *   → onFinalize() is fired once when upstream first returns 0 (EOF)
 *   → subsequent read() calls return 0 immediately
 *
 * ## Thread safety
 *
 * Same as AudioStream: not thread-safe; must be driven by a single consumer thread.
 */
class AnalyzerStream : public AudioStream {
public:
    AnalyzerStream(std::unique_ptr<AudioStream> upstream, NodeContext& ctx);
    ~AnalyzerStream() override = default;

    /// Pass-through: forwards every frame to the Sink unchanged.
    /// Fires onSamples() on each filled buffer; fires onFinalize() at EOF.
    size_t read(float* buf, size_t frames) override final;

    const AudioFormat& format() const override;

protected:
    /// Called for every non-empty buffer returned by upstream.
    /// Default is a no-op — override to accumulate inline statistics.
    virtual void onSamples(const float* /*buf*/, size_t /*frames*/) {}

    /// Called exactly once at EOF.  Must build and emit the Report.
    virtual void onFinalize() = 0;

    std::unique_ptr<AudioStream> upstream_;
    NodeContext& ctx_;

private:
    bool finalized_ = false;
};

// --------------------------------------------------------------------------
// AnalyzerNode
// --------------------------------------------------------------------------

/**
 * @brief Base class for analysis nodes — StreamProcessorNodes that observe
 *        the audio signal and emit Reports without modifying it.
 *
 * Sits at the same architectural level as DSPNode.  Provides:
 *   - primaryInput() / primaryOutput() → PortType::AudioStream.
 *   - nodeHint() → "Analyzer" (used by the UI for categorisation).
 *
 * Subclasses must implement:
 *   - id() / name()    via REGISTER_NODE / NODE_ID_AND_NAME_GETTER_DEFINITION
 *   - init()           configure from string-string parameter map (TOML persistence)
 *   - exportConfig()   serialise parameters back to map
 *   - wrap()           construct and return a concrete AnalyzerStream subclass
 *   - drawUI()         ImGui parameter widgets shown in the Action Editor
 */
class AnalyzerNode : public Node, public StreamProcessorNode {
public:
    AnalyzerNode() = default;

    std::string nodeHint() const override { return "Analyzer"; }

    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::AudioStream; }
};
