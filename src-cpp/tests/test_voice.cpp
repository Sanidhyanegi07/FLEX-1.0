#include "flex/voice/audio_buffer.hpp"
#include "flex/voice/audio_capture_service.hpp"
#include "flex/voice/text_to_speech_service.hpp"
#include "flex/voice/tts_queue.hpp"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>

#include <cassert>
#include <cmath>
#include <cstring>
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

    void simulateDeviceError(flex::voice::AudioCaptureError err, const QString& msg)
    {
        m_isRecording = false;
        if (m_errorCallback) {
            m_errorCallback(err, msg);
        }
    }

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
        notifyStarted();
        return true;
    }

    void stop() override
    {
        m_isSpeaking = false;
        m_stopCalledCount++;
        notifyStopped();
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
    mockSource->setSimulatedSamples(16000, 2000);

    {
        flex::voice::AudioCaptureService service(mockSource);
        service.startCapture();
        EXPECT_TRUE(service.isRecording());
        // Destruction happens when service goes out of scope here
    }

    EXPECT_FALSE(mockSource->isRecording());
}

void test_audio_capture_duration_timeout()
{
    std::cout << "[Test 6] Audio capture 30s timeout and automatic stop...\n";
    auto mockSource = std::make_shared<MockAudioCaptureSource>();
    mockSource->setSimulatedSamples(16000, 1000);

    flex::voice::AudioCaptureService service(mockSource);
    // Inject a 40ms duration limit for fast deterministic test execution
    service.setMaxDurationMs(40);
    EXPECT_EQ(40, service.maxDurationMs());
    EXPECT_FALSE(service.wasMaxDurationReached());

    bool timeoutFired = false;
    QObject::connect(&service, &flex::voice::AudioCaptureService::captureMaxDurationReached, [&]() {
        timeoutFired = true;
    });

    bool finishedFired = false;
    size_t samplesReceived = 0;
    QObject::connect(&service, &flex::voice::AudioCaptureService::captureFinished, [&](const flex::voice::AudioBuffer& buf) {
        finishedFired = true;
        samplesReceived = buf.sampleCount();
    });

    service.startCapture();
    EXPECT_TRUE(service.isRecording());

    // Wait for timer to trigger auto-stop
    QEventLoop loop;
    QTimer::singleShot(80, &loop, &QEventLoop::quit);
    loop.exec();

    EXPECT_FALSE(service.isRecording());
    EXPECT_EQ(static_cast<int>(flex::voice::AudioCaptureState::Idle), static_cast<int>(service.state()));
    EXPECT_TRUE(timeoutFired);
    EXPECT_TRUE(service.wasMaxDurationReached());
    EXPECT_TRUE(finishedFired);
    EXPECT_EQ(std::size_t(16000), samplesReceived);
}

void test_audio_capture_device_disconnection_error()
{
    std::cout << "[Test 7] Unexpected device disconnection and error handling...\n";
    auto mockSource = std::make_shared<MockAudioCaptureSource>();
    flex::voice::AudioCaptureService service(mockSource);

    bool errorFired = false;
    flex::voice::AudioCaptureError receivedError = flex::voice::AudioCaptureError::None;
    QObject::connect(&service, &flex::voice::AudioCaptureService::captureError, [&](flex::voice::AudioCaptureError err, const QString&) {
        errorFired = true;
        receivedError = err;
    });

    service.startCapture();
    EXPECT_TRUE(service.isRecording());

    // Simulate hardware disconnection mid-recording
    mockSource->simulateDeviceError(flex::voice::AudioCaptureError::DeviceUnavailable, "Device removed");

    EXPECT_FALSE(service.isRecording());
    EXPECT_EQ(static_cast<int>(flex::voice::AudioCaptureState::Error), static_cast<int>(service.state()));
    EXPECT_EQ(static_cast<int>(flex::voice::AudioCaptureError::DeviceUnavailable), static_cast<int>(service.lastError()));
    EXPECT_TRUE(errorFired);
    EXPECT_EQ(static_cast<int>(flex::voice::AudioCaptureError::DeviceUnavailable), static_cast<int>(receivedError));
}

// -----------------------------------------------------------------------------
// Audio Resampling & Conversion Tests (Task 1)
// -----------------------------------------------------------------------------

void test_audio_conversion_16k_int16()
{
    std::cout << "[Test 8] Audio format conversion: 16 kHz signed 16-bit PCM...\n";
    const int sampleRate = 16000;
    const int numSamples = 16000; // 1.0 second
    std::vector<int16_t> pcmInput(numSamples);

    for (int i = 0; i < numSamples; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * (static_cast<float>(i) / 16000.0f)) * 0.75f;
        pcmInput[i] = static_cast<int16_t>(val * 32767.0f);
    }

    auto res = flex::voice::convertAndResampleTo16kMono(
        pcmInput.data(),
        pcmInput.size() * sizeof(int16_t),
        sampleRate,
        1,
        flex::voice::AudioSampleFormat::Int16,
        16000);

    EXPECT_TRUE(res.success);
    EXPECT_EQ(16000, res.buffer.sampleRate);
    EXPECT_EQ(1, res.buffer.channelCount);
    EXPECT_EQ(std::size_t(16000), res.buffer.sampleCount());
    EXPECT_EQ(1000, res.buffer.durationMs);

    // Verify normalization
    for (float s : res.buffer.samples) {
        EXPECT_TRUE(s >= -1.0f && s <= 1.0f);
    }
}

void test_audio_conversion_44k1_float()
{
    std::cout << "[Test 9] Audio format conversion: 44.1 kHz float mono resampling...\n";
    const int sampleRate = 44100;
    const int numSamples = 22050; // 0.5 seconds
    std::vector<float> floatInput(numSamples);

    for (int i = 0; i < numSamples; ++i) {
        floatInput[i] = std::sin(2.0f * 3.14159265f * 880.0f * (static_cast<float>(i) / 44100.0f)) * 0.5f;
    }

    auto res = flex::voice::convertAndResampleTo16kMono(
        floatInput.data(),
        floatInput.size() * sizeof(float),
        sampleRate,
        1,
        flex::voice::AudioSampleFormat::Float32,
        16000);

    EXPECT_TRUE(res.success);
    EXPECT_EQ(16000, res.buffer.sampleRate);
    EXPECT_EQ(1, res.buffer.channelCount);

    // Expected sample count: round(22050 * 16000 / 44100) = 8000. Rounding tolerance: +/- 1 sample
    size_t expected = static_cast<size_t>(std::round(22050.0 * 16000.0 / 44100.0));
    EXPECT_EQ(std::size_t(8000), expected);
    EXPECT_EQ(expected, res.buffer.sampleCount());
    EXPECT_EQ(500, res.buffer.durationMs);

    // Check sample validity
    for (float s : res.buffer.samples) {
        EXPECT_TRUE(s >= -1.0f && s <= 1.0f);
        EXPECT_FALSE(std::isnan(s));
    }
}

void test_audio_conversion_48k_int16_stereo()
{
    std::cout << "[Test 10] Audio format conversion: 48 kHz 16-bit stereo downmix and resampling...\n";
    const int sampleRate = 48000;
    const int numFrames = 48000; // 1.0 second of stereo
    const int channels = 2;
    std::vector<int16_t> stereoInput(numFrames * channels);

    for (int i = 0; i < numFrames; ++i) {
        float left = std::sin(2.0f * 3.14159265f * 440.0f * (static_cast<float>(i) / 48000.0f)) * 0.5f;
        float right = std::sin(2.0f * 3.14159265f * 880.0f * (static_cast<float>(i) / 48000.0f)) * 0.5f;
        stereoInput[i * 2] = static_cast<int16_t>(left * 32767.0f);
        stereoInput[i * 2 + 1] = static_cast<int16_t>(right * 32767.0f);
    }

    auto res = flex::voice::convertAndResampleTo16kMono(
        stereoInput.data(),
        stereoInput.size() * sizeof(int16_t),
        sampleRate,
        channels,
        flex::voice::AudioSampleFormat::Int16,
        16000);

    EXPECT_TRUE(res.success);
    EXPECT_EQ(16000, res.buffer.sampleRate);
    EXPECT_EQ(1, res.buffer.channelCount);
    // 48000 frames * 16000 / 48000 = exactly 16000 mono output samples
    EXPECT_EQ(std::size_t(16000), res.buffer.sampleCount());
    EXPECT_EQ(1000, res.buffer.durationMs);

    for (float s : res.buffer.samples) {
        EXPECT_TRUE(s >= -1.0f && s <= 1.0f);
        EXPECT_FALSE(std::isnan(s));
    }
}

void test_audio_conversion_invalid_and_unsupported_formats()
{
    std::cout << "[Test 11] Audio format conversion: invalid, unsupported & truncated format rejection...\n";

    // 1. Empty input
    auto resEmpty = flex::voice::convertAndResampleTo16kMono(
        nullptr, 0, 16000, 1, flex::voice::AudioSampleFormat::Int16);
    EXPECT_TRUE(resEmpty.success);
    EXPECT_TRUE(resEmpty.buffer.isEmpty());
    EXPECT_EQ(std::size_t(0), resEmpty.buffer.sampleCount());

    // 2. Unknown format
    int16_t dummyData[10] = {0};
    auto resUnknown = flex::voice::convertAndResampleTo16kMono(
        dummyData, sizeof(dummyData), 16000, 1, flex::voice::AudioSampleFormat::Unknown);
    EXPECT_FALSE(resUnknown.success);
    EXPECT_FALSE(resUnknown.errorMessage.empty());

    // 3. Invalid sample rate
    auto resBadRate = flex::voice::convertAndResampleTo16kMono(
        dummyData, sizeof(dummyData), 0, 1, flex::voice::AudioSampleFormat::Int16);
    EXPECT_FALSE(resBadRate.success);

    // 4. Invalid channels
    auto resBadCh = flex::voice::convertAndResampleTo16kMono(
        dummyData, sizeof(dummyData), 16000, 0, flex::voice::AudioSampleFormat::Int16);
    EXPECT_FALSE(resBadCh.success);

    // 5. Truncated byte length (3 bytes for 16-bit mono is truncated)
    char oddBytes[3] = {0, 0, 0};
    auto resTrunc = flex::voice::convertAndResampleTo16kMono(
        oddBytes, sizeof(oddBytes), 16000, 1, flex::voice::AudioSampleFormat::Int16);
    EXPECT_FALSE(resTrunc.success);
    EXPECT_FALSE(resTrunc.errorMessage.empty());
}

void test_audio_conversion_silence_nonzero_and_clipping()
{
    std::cout << "[Test 12] Audio format conversion: silence, non-zero signal & clipping/finite limits...\n";

    // Silence
    std::vector<int16_t> silence(1000, 0);
    auto resSilence = flex::voice::convertAndResampleTo16kMono(
        silence.data(), silence.size() * sizeof(int16_t), 16000, 1, flex::voice::AudioSampleFormat::Int16);
    EXPECT_TRUE(resSilence.success);
    for (float s : resSilence.buffer.samples) {
        EXPECT_EQ(0.0f, s);
    }

    // Clipping and NaN protection for float input
    std::vector<float> noisyFloats = {
        2.5f, -3.0f, 0.5f, -0.5f,
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity()
    };

    auto resClipped = flex::voice::convertAndResampleTo16kMono(
        noisyFloats.data(), noisyFloats.size() * sizeof(float), 16000, 1, flex::voice::AudioSampleFormat::Float32);
    EXPECT_TRUE(resClipped.success);
    for (float s : resClipped.buffer.samples) {
        EXPECT_FALSE(std::isnan(s));
        EXPECT_TRUE(s >= -1.0f && s <= 1.0f);
    }
}

// -----------------------------------------------------------------------------
// Text-to-Speech Tests
// -----------------------------------------------------------------------------

void test_tts_service_lifecycle_and_mock_synthesis()
{
    std::cout << "[Test 13] TTS service lifecycle and synthesis...\n";
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

void test_tts_completion_propagation()
{
    std::cout << "[Test 14] TTS completion signal propagation...\n";
    auto mockEngine = std::make_shared<MockTtsEngine>();
    flex::voice::TextToSpeechService service(mockEngine);

    bool finishedEmitted = false;
    QObject::connect(&service, &flex::voice::TextToSpeechService::speechFinished, [&]() {
        finishedEmitted = true;
    });

    service.speak("Sentence completing normally.");
    EXPECT_TRUE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Speaking), static_cast<int>(service.state()));

    // Engine notifies that speech finished
    mockEngine->notifyFinished();

    EXPECT_FALSE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Idle), static_cast<int>(service.state()));
    EXPECT_TRUE(finishedEmitted);
}

void test_tts_stop_and_cancellation()
{
    std::cout << "[Test 15] TTS stop, cancellation and queue purging...\n";
    auto mockEngine = std::make_shared<MockTtsEngine>();
    flex::voice::TextToSpeechService service(mockEngine);

    bool cancelledEmitted = false;
    QObject::connect(&service, &flex::voice::TextToSpeechService::speechCancelled, [&]() {
        cancelledEmitted = true;
    });

    service.speak("Sentence one.");
    EXPECT_TRUE(service.isSpeaking());

    service.stop();
    EXPECT_FALSE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Idle), static_cast<int>(service.state()));
    EXPECT_EQ(1, mockEngine->stopCalledCount());
    EXPECT_TRUE(cancelledEmitted);

    // Repeated stop should be safe
    service.stop();
    EXPECT_FALSE(service.isSpeaking());
}

void test_tts_engine_failure_handling()
{
    std::cout << "[Test 16] TTS engine failure reporting...\n";
    auto mockEngine = std::make_shared<MockTtsEngine>();
    mockEngine->setFailSpeak(true);

    flex::voice::TextToSpeechService service(mockEngine);
    bool ok = service.speak("Attempted speech");
    EXPECT_FALSE(ok);
    EXPECT_FALSE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Error), static_cast<int>(service.state()));
    EXPECT_EQ(static_cast<int>(flex::voice::TtsError::SynthesisFailed), static_cast<int>(service.lastError()));

    // Test async error reporting through notifyError
    mockEngine->setFailSpeak(false);
    service.speak("Second attempt");
    EXPECT_TRUE(service.isSpeaking());

    bool errorEmitted = false;
    QString errorMsg;
    QObject::connect(&service, &flex::voice::TextToSpeechService::speechError, [&](flex::voice::TtsError, const QString& msg) {
        errorEmitted = true;
        errorMsg = msg;
    });

    mockEngine->notifyError("Direct hardware sound error");
    EXPECT_FALSE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Error), static_cast<int>(service.state()));
    EXPECT_TRUE(errorEmitted);
    EXPECT_FALSE(errorMsg.isEmpty());
}

void test_tts_queue_fifo_and_clearing()
{
    std::cout << "[Test 17] TTS Queue FIFO ordering and clearing on cancellation...\n";
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
    std::cout << "[Test 18] Real Windows SAPI engine initialization smoke test...\n";
    flex::voice::TextToSpeechService service; // Constructs default SapiTtsEngine
    EXPECT_FALSE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Idle), static_cast<int>(service.state()));

    // Test SAPI initialization by calling speak on a non-blocking background phrase
    bool ok = service.speak("FLEX SAPI smoke initialization test.", true);
    // On systems with SAPI installed, speak should succeed and transition state
    if (ok) {
        EXPECT_TRUE(service.isSpeaking());
        // Cleanly stop and purge
        service.stop();
        EXPECT_FALSE(service.isSpeaking());
        EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Idle), static_cast<int>(service.state()));
    }
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
    test_audio_capture_duration_timeout();
    test_audio_capture_device_disconnection_error();

    test_audio_conversion_16k_int16();
    test_audio_conversion_44k1_float();
    test_audio_conversion_48k_int16_stereo();
    test_audio_conversion_invalid_and_unsupported_formats();
    test_audio_conversion_silence_nonzero_and_clipping();

    test_tts_service_lifecycle_and_mock_synthesis();
    test_tts_completion_propagation();
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
