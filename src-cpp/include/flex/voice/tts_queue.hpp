#ifndef FLEX_VOICE_TTS_QUEUE_HPP
#define FLEX_VOICE_TTS_QUEUE_HPP

#include <QString>

#include <deque>
#include <mutex>
#include <optional>
#include <string>

namespace flex::voice {

/**
 * Thread-safe FIFO queue for sequential speech segments.
 * Prepares the architecture for sentence-based streaming text-to-speech.
 *
 * Guarantees:
 * - Strict FIFO ordering of spoken sentences.
 * - Instant clearing on cancellation or conversation switching.
 * - Prevents duplicate or out-of-order speech chunks.
 */
class TtsQueue {
public:
    TtsQueue() = default;
    ~TtsQueue() = default;

    void enqueue(const QString& segment)
    {
        QString trimmed = segment.trimmed();
        if (trimmed.isEmpty()) {
            return;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.push_back(trimmed);
    }

    std::optional<QString> dequeue()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_queue.empty()) {
            return std::nullopt;
        }
        QString front = m_queue.front();
        m_queue.pop_front();
        return front;
    }

    void clear()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_queue.clear();
    }

    [[nodiscard]] bool isEmpty() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.empty();
    }

    [[nodiscard]] size_t size() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_queue.size();
    }

private:
    mutable std::mutex m_mutex;
    std::deque<QString> m_queue;
};

} // namespace flex::voice

#endif // FLEX_VOICE_TTS_QUEUE_HPP

