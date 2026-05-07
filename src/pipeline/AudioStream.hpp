#pragma once

#include <cstddef>

struct AudioFormat {
    double sampleRate = 0.0;
    int    channels   = 0;
    int    bitDepth   = 0;  // bit depth of original source (e.g. 16, 24, 32)
};

/**
 * @brief Pull-based audio stream interface.
 *
 * Consumers call read() to request frames. Sources/processors only produce
 * data on demand — no upstream push ever occurs.
 *
 * ## Pipeline topology
 *
 * Streams are composed via the Decorator pattern: a processor stream holds a
 * `unique_ptr<AudioStream>` to its upstream and forwards read() calls up the
 * chain, transforming the data on the way back down. The driving force is
 * always the sink — it calls read() in a loop, which recursively pulls data
 * through every layer until it reaches the source.
 *
 * @code
 *   OutputSinkNode                          ← drives the loop
 *     └─ DSPStream (e.g. ChannelMix)        ← wraps and transforms
 *          └─ DSPStream (e.g. PeakNormalize)
 *               └─ AudioFileStream          ← reads from disk (no upstream)
 * @endcode
 *
 * ## Design rationale: upstream is NOT stored in the base class
 *
 * Not every concrete stream has an upstream:
 *   - @c AudioFileStream reads from a file handle — it has no upstream.
 *   - A future mixer node would need *two* upstreams.
 *   - @c TruePeakLimiterStream drains its upstream entirely during
 *     construction and does not hold it afterwards.
 *
 * Storing upstream here would force a field on subclasses that don't need it
 * and would break multi-input topologies. Each subclass manages its own
 * upstream lifetime as a private implementation detail.
 *
 * Similarly, @c format() is intentionally not given a default implementation
 * that delegates to an upstream, because format-converting nodes (e.g.
 * ResamplerNode) must return a *modified* format rather than forwarding the
 * upstream's.
 */
class AudioStream {
public:
    virtual ~AudioStream() = default;

    /**
     * @brief Pull up to `frames` interleaved float frames into `buffer`.
     *
     * Implementations should pull from their upstream (if any), process the
     * data in-place or into scratch buffers, and write the result back into
     * @p buffer. All channels are interleaved: for a stereo stream the layout
     * is [L0, R0, L1, R1, ...].
     *
     * @param buffer  Caller-allocated output buffer; must hold at least
     *                `frames * format().channels` floats.
     * @param frames  Maximum number of frames to produce.
     * @return Number of frames actually written. Returns 0 at EOF.
     */
    virtual size_t read(float* buffer, size_t frames) = 0;

    /**
     * @brief Format of data produced by this stream.
     *
     * Must remain stable for the lifetime of the stream. Format-converting
     * nodes (e.g. resampler, channel mixer) return their *output* format here,
     * not the upstream's.
     */
    virtual const AudioFormat& format() const = 0;
};
