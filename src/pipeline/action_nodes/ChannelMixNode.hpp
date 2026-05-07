#pragma once

#include "../Node.hpp"

/**
 * @brief Stream processor that remaps audio channels according to a selected mix mode.
 *
 * Unlike DSPNode-based processors, this node changes the channel count of the
 * stream.  It therefore owns a custom AudioStream subclass (ChannelMixStream)
 * that reports the output channel count via format(), ensuring all downstream
 * nodes (ResamplerNode, OutputSinkNode, …) see the correct layout.
 *
 * ### Supported modes
 * | Mode                | Input      | Output | Algorithm                             |
 * |---------------------|------------|--------|---------------------------------------|
 * | `stereo_to_mono`    | exactly 2  | 1      | (L + R) × 0.5                        |
 * | `mono_to_stereo`    | exactly 1  | 2      | L = R = input                        |
 * | `surround_to_stereo`| 6 or 8 ch  | 2      | ITU-R BS.775 downmix matrix           |
 * | `to_mono`           | any N      | 1      | Equal-weight average of all channels  |
 *
 * ### Error handling
 * If the upstream channel count is incompatible with the selected mode, wrap()
 * throws std::runtime_error.  ChainEngine::processFile() catches and logs it;
 * other files in the batch continue processing normally.
 *
 * ### init() parameters
 * | Key    | Values                                                    | Default          |
 * |--------|-----------------------------------------------------------|------------------|
 * | `mode` | `stereo_to_mono`, `mono_to_stereo`, `surround_to_stereo`, | `stereo_to_mono` |
 * |        | `to_mono`                                                 |                  |
 */
class ChannelMixNode : public Node, public StreamProcessorNode {
public:
    enum class MixMode {
        StereoToMono      = 0,
        MonoToStereo      = 1,
        SurroundToStereo  = 2,
        ToMono            = 3,
    };

    ChannelMixNode() = default;

    std::string name() const override { return "Channel Mix"; }
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
        height = 98.0f;
    }

private:
    MixMode mode_ = MixMode::StereoToMono;
};
