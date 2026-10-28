#ifndef FLEX_VOICE_AUDIO_BUFFER_HPP
#define FLEX_VOICE_AUDIO_BUFFER_HPP

#include <QByteArray>
#include <cstdint>
#include <vector>

namespace flex::voice {

/**
 * Standard in-memory audio buffer representation.
 * Formatted as 16 kHz 16-bit mono PCM (normalized float samples -1.0f to 1.0f),
 * matching the input specification required by speech-to-text engines.
 */
struct AudioBuffer {
    int sampleRate{16000};
    int channelCount{1};
    int sampleSizeBits{16};
    std::vector<float> samples; // Normalized [-1.0f, 1.0f]
    QByteArray rawPcmBytes;      // Raw signed 16-bit little-endian PCM
    int64_t durationMs{0};

    [[nodiscard]] bool isEmpty() const noexcept
    {
        return samples.empty();
    }

    [[nodiscard]] size_t sampleCount() const noexcept
    {
        return samples.size();
    }

    void clear()
    {
        samples.clear();
        rawPcmBytes.clear();
        durationMs = 0;
    }
};

} // namespace flex::voice

#endif // FLEX_VOICE_AUDIO_BUFFER_HPP

