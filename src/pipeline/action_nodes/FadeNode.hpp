#pragma once

#include "../Node.hpp"

#include <string>
#include <unordered_map>

/**
 * @brief Stream processor that applies fade-in and/or fade-out envelopes to audio.
 *
 * The node buffers the entire upstream during wrap() so that the total frame
 * count is known before replay begins (required for fade-out positioning).
 * Each parameter set — duration and curve shape — is independently configurable
 * for the fade-in and fade-out sides.
 *
 * Setting a duration to 0 disables that side entirely (pass-through for that
 * portion of the signal).  If the combined fade durations would exceed the
 * total file length, each side is clamped to at most half the total duration
 * so they never overlap.
 *
 * ### Curve shapes
 * | Value         | Fade-in gain (t ∈ [0,1])         | Character                         |
 * |---------------|----------------------------------|-----------------------------------|
 * | `linear`      | t                                | Constant rate of change           |
 * | `sine`        | sin(t × π/2)                     | Slow start, smooth finish         |
 * | `exponential` | t²                               | Very slow start, rapid finish     |
 *
 * For fade-out the same curve is applied in reverse (t runs from 1 → 0).
 *
 * ### init() parameters
 * | Key              | Default | Range        | Description                     |
 * |------------------|---------|--------------|---------------------------------|
 * | `fade_in_ms`     |   0.0   | 0 .. 30000   | Fade-in duration (ms)           |
 * | `fade_out_ms`    |   0.0   | 0 .. 30000   | Fade-out duration (ms)          |
 * | `fade_in_curve`  | `sine`  | see above    | Fade-in curve shape             |
 * | `fade_out_curve` | `sine`  | see above    | Fade-out curve shape            |
 */
class FadeNode : public Node, public StreamProcessorNode {
public:
    enum class CurveType : int {
        Linear      = 0,  ///< Constant rate of change
        Sine        = 1,  ///< sin(t × π/2) — equal-power style, smooth tail
        Exponential = 2,  ///< t² — very slow start, accelerates toward full level
    };

    FadeNode() = default;

    std::string name() const override { return "Fade"; }

    void init(const std::unordered_map<std::string, std::string>& params) override;

    PortType primaryInput()  const override { return PortType::AudioStream; }
    PortType primaryOutput() const override { return PortType::AudioStream; }

    std::unique_ptr<AudioStream> wrap(
        std::unique_ptr<AudioStream> upstream,
        NodeContext& ctx) override;

    void drawUI() override;
    void getUiSize(float& width, float& height) override
    {
        width  = 0.0f;
        height = 240.0f + 8.0f;
    }

private:
    float     fadeInMs_      = 0.0f;
    float     fadeOutMs_     = 0.0f;
    CurveType fadeInCurve_   = CurveType::Sine;
    CurveType fadeOutCurve_  = CurveType::Sine;
};
