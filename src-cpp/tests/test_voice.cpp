#include "flex/voice/audio_buffer.hpp"
#include "flex/voice/audio_capture_service.hpp"
#include "flex/voice/text_to_speech_service.hpp"
#include "flex/voice/tts_queue.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cassert>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

int g_tests_run = 0;
int g_tests_passed = 0;
int g_tests_failed = 0;

#define EXPECT_TRUE(cond) do { \
    g_tests_run++; \
    if (cond) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " (" #cond " is false)\n"; \
    } \
} while (0)

#define EXPECT_FALSE(cond) do { \
    g_tests_run++; \
    if (!(cond)) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ << " (" #cond " is true)\n"; \
    } \
} while (0)

#define EXPECT_EQ(expected, actual) do { \
    g_tests_run++; \
    auto _exp = (expected); \
    auto _act = (actual); \
    if (_exp == _act) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ \
                  << " (expected " << _exp << ", got " << _act << ")\n"; \
    } \
} while (0)

#define EXPECT_STR_EQ(expected, actual) do { \
    g_tests_run++; \
    std::string _exp = (expected); \
    std::string _act = (actual); \
    if (_exp == _act) { \
        g_tests_passed++; \
    } else { \
        g_tests_failed++; \
        std::cerr << "FAIL: " << __FILE__ << ":" << __LINE__ \
                  << " (expected '" << _exp << "', got '" << _act << "')\n"; \
    } \
} while (0)

// -----------------------------------------------------------------------------
// Mock Audio Capture Source for Headless CI Testing
// -----------------------------------------------------------------------------

class MockAudioCaptureSource : public flex::voice::IAudioCaptureSource {
public:
    MockAudioCaptureSource() = default;

    void setSimulatedSamples(int sampleRate, int durationMs, float frequency = 440.0f)
    {
        m_simulatedBuffer.sampleRate = sampleRate;
        m_simulatedBuffer.channelCount = 1;
        m_simulatedBuffer.sampleSizeBits = 16;
        m_simulatedBuffer.durationMs = durationMs;

        int totalSamples = (sampleRate * durationMs) / 1000;
        m_simulatedBuffer.samples.resize(totalSamples);
        m_simulatedBuffer.rawPcmBytes.resize(totalSamples * sizeof(int16_t));

        auto* rawPtr = reinterpret_cast<int16_t*>(m_simulatedBuffer.rawPcmBytes.data());
        for (int i = 0; i < totalSamples; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(sampleRate);
            float val = std::sin(2.0f * 3.14159265f * frequency * t) * 0.5f;
            m_simulatedBuffer.samples[i] = val;
            rawPtr[i] = static_cast<int16_t>(val * 32767.0f);
        }
    }

    void setFailStart(bool fail) { m_failStart = fail; }
    void setReturnEmpty(bool empty) { m_returnEmpty = empty; }

    bool start() override
    {
        if (m_failStart) {
            return false;
        }
        m_isRecording = true;
        return true;
    }

    flex::voice::AudioBuffer stop() override
    {
        m_isRecording = false;
        if (m_returnEmpty) {
            return flex::voice::AudioBuffer{};
        }
        return m_simulatedBuffer;
    }

    void cancel() override
    {
        m_isRecording = false;
    }

    [[nodiscard]] bool isRecording() const override
    {
        return m_isRecording;
    }

    [[nodiscard]] std::vector<flex::voice::AudioDeviceInfo> availableDevices() const override
    {
        return {
            { "mock-mic-1", "Mock Studio Microphone", true },
            { "mock-mic-2", "Mock Headset Microphone", false }
        };
    }

    [[nodiscard]] flex::voice::AudioDeviceInfo defaultDevice() const override
    {
        return { "mock-mic-1", "Mock Studio Microphone", true };
    }

private:
    bool m_isRecording{false};
    bool m_failStart{false};
    bool m_returnEmpty{false};
    flex::voice::AudioBuffer m_simulatedBuffer;
};

// -----------------------------------------------------------------------------
// Mock Text-to-Speech Engine for Headless CI Testing
// -----------------------------------------------------------------------------

class MockTtsEngine : public flex::voice::ITextToSpeechEngine {
public:
    MockTtsEngine() = default;

    void setFailInitialize(bool fail) { m_failInit = fail; }
    void setFailSpeak(bool fail) { m_failSpeak = fail; }

    bool initialize() override
    {
        if (m_failInit) {
            return false;
        }
        m_initialized = true;
        return true;
    }

    bool speak(const std::string& text, bool purgePrevious) override
    {
        if (m_failSpeak || !m_initialized) {
            return false;
        }
        if (purgePrevious) {
            m_spokenTexts.clear();
        }
        m_spokenTexts.push_back(text);
        m_isSpeaking = true;
        return true;
    }

    void stop() override
    {
        m_isSpeaking = false;
        m_stopCalledCount++;
    }

    [[nodiscard]] bool isSpeaking() const override
    {
        return m_isSpeaking;
    }

    [[nodiscard]] const std::vector<std::string>& spokenTexts() const { return m_spokenTexts; }
    [[nodiscard]] int stopCalledCount() const { return m_stopCalledCount; }

private:
    bool m_initialized{true};
    bool m_failInit{false};
    bool m_failSpeak{false};
    bool m_isSpeaking{false};
    int m_stopCalledCount{0};
    std::vector<std::string> m_spokenTexts;
};

// -----------------------------------------------------------------------------
// Audio Capture Tests
// -----------------------------------------------------------------------------

void test_audio_capture_device_discovery()
{
    std::cout << "[Test 1] Audio capture device discovery...\n";
    auto mockSource = std::make_shared<MockAudioCaptureSource>();
    flex::voice::AudioCaptureService service(mockSource);

    auto devices = service.availableInputDevices();
    EXPECT_EQ(std::size_t(2), devices.size());
    EXPECT_STR_EQ("Mock Studio Microphone", devices[0].name);
    EXPECT_TRUE(devices[0].isDefault);

    auto defDev = service.defaultInputDevice();
    EXPECT_STR_EQ("mock-mic-1", defDev.id);
}

void test_audio_capture_start_stop_lifecycle()
{
    std::cout << "[Test 2] Audio capture start/stop lifecycle and sample delivery...\n";
    auto mockSource = std::make_shared<MockAudioCaptureSource>();
    mockSource->setSimulatedSamples(16000, 1000, 440.0f); // 1.0 second of 16kHz audio

    flex::voice::AudioCaptureService service(mockSource);
    EXPECT_FALSE(service.isRecording());
    EXPECT_EQ(static_cast<int>(flex::voice::AudioCaptureState::Idle), static_cast<int>(service.state()));

    bool started = service.startCapture();
    EXPECT_TRUE(started);
    EXPECT_TRUE(service.isRecording());
    EXPECT_EQ(static_cast<int>(flex::voice::AudioCaptureState::Recording), static_cast<int>(service.state()));

    // Repeated start call while recording should be safe no-op
    bool startAgain = service.startCapture();
    EXPECT_TRUE(startAgain);
    EXPECT_TRUE(service.isRecording());

    auto buffer = service.stopCapture();
    EXPECT_FALSE(service.isRecording());
    EXPECT_EQ(static_cast<int>(flex::voice::AudioCaptureState::Idle), static_cast<int>(service.state()));

    EXPECT_FALSE(buffer.isEmpty());
    EXPECT_EQ(std::size_t(16000), buffer.sampleCount());
    EXPECT_EQ(16000, buffer.sampleRate);
    EXPECT_EQ(1, buffer.channelCount);
    EXPECT_EQ(1000, buffer.durationMs);
}

void test_audio_capture_empty_and_cancellation()
{
    std::cout << "[Test 3] Empty recording handling and cancellation...\n";
    auto mockSource = std::make_shared<MockAudioCaptureSource>();
    mockSource->setReturnEmpty(true);

    flex::voice::AudioCaptureService service(mockSource);
    service.startCapture();
    EXPECT_TRUE(service.isRecording());

    auto buffer = service.stopCapture();
    EXPECT_TRUE(buffer.isEmpty());
    EXPECT_EQ(std::size_t(0), buffer.sampleCount());

    // Test cancellation
    mockSource->setReturnEmpty(false);
    mockSource->setSimulatedSamples(16000, 500);
    service.startCapture();
    EXPECT_TRUE(service.isRecording());

    service.cancelCapture();
    EXPECT_FALSE(service.isRecording());
    EXPECT_EQ(static_cast<int>(flex::voice::AudioCaptureState::Idle), static_cast<int>(service.state()));

    // Repeated cancel should be safe
    service.cancelCapture();
    EXPECT_FALSE(service.isRecording());
}

void test_audio_capture_hardware_error_handling()
{
    std::cout << "[Test 4] Audio capture hardware error handling...\n";
    auto mockSource = std::make_shared<MockAudioCaptureSource>();
    mockSource->setFailStart(true);

    flex::voice::AudioCaptureService service(mockSource);
    bool started = service.startCapture();
    EXPECT_FALSE(started);
    EXPECT_FALSE(service.isRecording());
    EXPECT_EQ(static_cast<int>(flex::voice::AudioCaptureState::Error), static_cast<int>(service.state()));
    EXPECT_EQ(static_cast<int>(flex::voice::AudioCaptureError::CaptureFailed), static_cast<int>(service.lastError()));
    EXPECT_FALSE(service.lastErrorMessage().isEmpty());
}

void test_audio_capture_safe_destruction_while_recording()
{
    std::cout << "[Test 5] Safe destruction while recording in progress...\n";
    auto mockSource = std::make_shared<MockAudioCaptureSource>();
    {
        flex::voice::AudioCaptureService service(mockSource);
        service.startCapture();
        EXPECT_TRUE(service.isRecording());
        // Destructor executes here
    }
    EXPECT_FALSE(mockSource->isRecording());
}

// -----------------------------------------------------------------------------
// Text-to-Speech Tests
// -----------------------------------------------------------------------------

void test_tts_service_lifecycle_and_mock_synthesis()
{
    std::cout << "[Test 6] TTS service lifecycle and synthesis...\n";
    auto mockEngine = std::make_shared<MockTtsEngine>();
    flex::voice::TextToSpeechService service(mockEngine);

    EXPECT_FALSE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Idle), static_cast<int>(service.state()));

    // Empty text should be safely rejected
    bool emptyRes = service.speak("");
    EXPECT_FALSE(emptyRes);
    EXPECT_FALSE(service.isSpeaking());

    bool whiteRes = service.speak("   \t\n ");
    EXPECT_FALSE(whiteRes);
    EXPECT_FALSE(service.isSpeaking());

    // Valid text synthesis
    bool speakRes = service.speak("FLEX 1.0 speech foundation operational.");
    EXPECT_TRUE(speakRes);
    EXPECT_TRUE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Speaking), static_cast<int>(service.state()));
    EXPECT_EQ(std::size_t(1), mockEngine->spokenTexts().size());
    EXPECT_STR_EQ("FLEX 1.0 speech foundation operational.", mockEngine->spokenTexts()[0]);
}

void test_tts_stop_and_cancellation()
{
    std::cout << "[Test 7] TTS stop, cancellation and queue purging...\n";
    auto mockEngine = std::make_shared<MockTtsEngine>();
    flex::voice::TextToSpeechService service(mockEngine);

    service.speak("Sentence one.");
    EXPECT_TRUE(service.isSpeaking());

    service.stop();
    EXPECT_FALSE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Idle), static_cast<int>(service.state()));
    EXPECT_EQ(1, mockEngine->stopCalledCount());

    // Repeated stop should be safe
    service.stop();
    EXPECT_FALSE(service.isSpeaking());
}

void test_tts_engine_failure_handling()
{
    std::cout << "[Test 8] TTS engine failure reporting...\n";
    auto mockEngine = std::make_shared<MockTtsEngine>();
    mockEngine->setFailSpeak(true);

    flex::voice::TextToSpeechService service(mockEngine);
    bool ok = service.speak("Attempted speech");
    EXPECT_FALSE(ok);
    EXPECT_FALSE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Error), static_cast<int>(service.state()));
    EXPECT_EQ(static_cast<int>(flex::voice::TtsError::SynthesisFailed), static_cast<int>(service.lastError()));
}

void test_tts_queue_fifo_and_clearing()
{
    std::cout << "[Test 9] TTS Queue FIFO ordering and clearing on cancellation...\n";
    flex::voice::TtsQueue queue;
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_EQ(std::size_t(0), queue.size());

    // Empty strings are ignored
    queue.enqueue("");
    queue.enqueue("   ");
    EXPECT_TRUE(queue.isEmpty());

    // Enqueue valid segments
    queue.enqueue("First sentence.");
    queue.enqueue("Second sentence.");
    queue.enqueue("Third sentence.");
    EXPECT_FALSE(queue.isEmpty());
    EXPECT_EQ(std::size_t(3), queue.size());

    // Dequeue preserves strict FIFO order
    auto s1 = queue.dequeue();
    EXPECT_TRUE(s1.has_value());
    EXPECT_STR_EQ("First sentence.", s1->toStdString());

    auto s2 = queue.dequeue();
    EXPECT_TRUE(s2.has_value());
    EXPECT_STR_EQ("Second sentence.", s2->toStdString());

    EXPECT_EQ(std::size_t(1), queue.size());

    // Clear queue (e.g. on user cancellation or conversation switch)
    queue.clear();
    EXPECT_TRUE(queue.isEmpty());
    EXPECT_EQ(std::size_t(0), queue.size());

    auto sEmpty = queue.dequeue();
    EXPECT_FALSE(sEmpty.has_value());
}

#ifdef _WIN32
void test_real_sapi_initialization_smoke()
{
    std::cout << "[Test 10] Real Windows SAPI engine initialization smoke test...\n";
    flex::voice::TextToSpeechService service; // Constructs default SapiTtsEngine
    // Check initial state without blocking or requiring physical audio
    EXPECT_FALSE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Idle), static_cast<int>(service.state()));
}
#endif

} // anonymous namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);

    std::cout << "========================================================\n";
    std::cout << " FLEX 1.0 Native Voice Subsystem Test Suite (Stage 6B)  \n";
    std::cout << "========================================================\n\n";

    test_audio_capture_device_discovery();
    test_audio_capture_start_stop_lifecycle();
    test_audio_capture_empty_and_cancellation();
    test_audio_capture_hardware_error_handling();
    test_audio_capture_safe_destruction_while_recording();
    test_tts_service_lifecycle_and_mock_synthesis();
    test_tts_stop_and_cancellation();
    test_tts_engine_failure_handling();
    test_tts_queue_fifo_and_clearing();

#ifdef _WIN32
    test_real_sapi_initialization_smoke();
#endif

    std::cout << "\n========================================================\n";
    std::cout << "Test Summary:\n";
    std::cout << "  Total Assertions : " << g_tests_run << "\n";
    std::cout << "  Passed           : " << g_tests_passed << "\n";
    std::cout << "  Failed           : " << g_tests_failed << "\n";
    std::cout << "========================================================\n";

    if (g_tests_failed > 0) {
        std::cerr << "Result: FAILED (" << g_tests_failed << " assertions failed)\n";
        return 1;
    }

    std::cout << "Result: SUCCESS (All native voice foundation tests passed!)\n";
    return 0;
}

