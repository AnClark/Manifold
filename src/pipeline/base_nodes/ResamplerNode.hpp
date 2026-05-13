#pragma once

#include "../Node.hpp"

#include <memory>
#include <vector>

// --------------------------------------------------------------------------
// ResamplerBackend — strategy interface
// --------------------------------------------------------------------------

/**
 * @brief Abstract resampler backend (Strategy pattern).
 *
 * A new backend instance is created for every file (via ResamplerNode::wrap),
 * ensuring zero cross-file state contamination.
 *
 * All audio is represented as **interleaved float** at the boundary.
 * The backend is responsible for any internal float↔double conversion.
 */
class ResamplerBackend {
public:
    virtual ~ResamplerBackend() = default;

    /** @brief Initialise for the given channel count and rate pair. */
    virtual void init(double srcRate, double dstRate, int channels) = 0;

    /**
     * @brief Process `inFrames` interleaved frames from `in`.
     * @param in       Interleaved input buffer (inFrames * channels floats).
     * @param inFrames Number of input frames.
     * @param out      Receives interleaved output frames (appended).
     */
    virtual void process(const float* in, size_t inFrames,
                         std::vector<float>& out) = 0;

    /**
     * @brief Flush residual samples after the last input block.
     * @param out Receives any remaining interleaved output frames (appended).
     */
    virtual void flush(std::vector<float>& out) = 0;
};

// --------------------------------------------------------------------------
// R8BrainBackend
// --------------------------------------------------------------------------

/**
 * @brief High-quality resampler backend using r8brain-free-src.
 *
 * One CDSPResampler24 object is maintained per channel. Internally converts
 * float↔double as required by the r8brain API.
 */
class R8BrainBackend : public ResamplerBackend {
public:
    R8BrainBackend();
    ~R8BrainBackend() override;

    void init(double srcRate, double dstRate, int channels) override;
    void process(const float* in, size_t inFrames, std::vector<float>& out) override;
    void flush(std::vector<float>& out) override;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    double srcRate_  = 44100.0;
    double dstRate_  = 48000.0;
    int    channels_ = 0;
    int    flushFrames_ = 1024;  ///< Frames of zeros needed to drain the filter tail (= filter latency)

    static constexpr int kMaxInLen = 1024;  ///< Must match the value used in init()

    /** @brief Internal helper: de-interleave + process all channels, then interleave output. */
    void processInterleaved(const float* in, size_t inFrames,
                            std::vector<float>& out);
};

// --------------------------------------------------------------------------
// LibSampleRateBackend (interface reservation — not yet implemented)
// --------------------------------------------------------------------------

/**
 * @brief Stub backend for libsamplerate (SRC).
 *
 * This class reserves the interface so that a libsamplerate backend can be
 * plugged in later without touching ResamplerNode or its consumers.
 * Calling any method currently throws std::logic_error.
 */
class LibSampleRateBackend : public ResamplerBackend {
public:
    void init(double srcRate, double dstRate, int channels) override;
    void process(const float* in, size_t inFrames, std::vector<float>& out) override;
    void flush(std::vector<float>& out) override;
};

// --------------------------------------------------------------------------
// ResamplerNode
// --------------------------------------------------------------------------

/**
 * @brief Stream-processor node that resamples to a target sample rate.
 *
 * Parameters:
 *   "rate"    — target sample rate in Hz (default: "48000")
 *   "backend" — "r8brain" (default) | "libsamplerate"
 *
 * The node creates a new ResamplerBackend instance on every wrap() call,
 * guaranteeing per-file state isolation.
 *
 * If the source rate already equals the target rate, the stream is returned
 * unchanged (passthrough).
 */
class ResamplerNode : public Node, public StreamProcessorNode {
public:
    NODE_ID_AND_NAME_GETTER_DEFINITION;
    void init(const std::unordered_map<std::string, std::string>& params) override;
    std::unordered_map<std::string, std::string> exportConfig() override;

    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::AudioStream; }

    void drawUI() override;
    void getUiSize(float& width, float& height) override
    {
        width  = 0.0f;
        height = 148.0f;
    }

    std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) override;

private:
    double      targetRate_ = 48000.0;
    std::string backendId_  = "r8brain";

    // UI state: indices into the preset arrays
    int rateIdx_    = 3;  ///< default points to 48000
    int backendIdx_ = 0;  ///< 0 = r8brain, 1 = libsamplerate

    std::unique_ptr<ResamplerBackend> makeBackend() const;
};
