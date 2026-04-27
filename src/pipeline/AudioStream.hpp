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
 */
class AudioStream {
public:
    virtual ~AudioStream() = default;

    /**
     * @brief Pull up to `frames` interleaved float frames into `buffer`.
     * @return Number of frames actually written. Returns 0 at EOF.
     */
    virtual size_t read(float* buffer, size_t frames) = 0;

    /** @brief Format of data produced by this stream. */
    virtual const AudioFormat& format() const = 0;
};
