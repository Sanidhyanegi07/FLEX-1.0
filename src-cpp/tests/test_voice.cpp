#include "flex/backend.hpp"
#include "flex/voice/audio_buffer.hpp"
#include "flex/voice/audio_capture_service.hpp"
#include "flex/voice/speech_to_text_service.hpp"
#include "flex/voice/text_to_speech_service.hpp"
#include "flex/voice/tts_queue.hpp"
#include "flex/voice/voice_conversation_controller.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QPointer>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <QUuid>

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
// In-Process Mock HTTP Server for Deterministic AI Backend Testing
// -----------------------------------------------------------------------------

class MockOllamaServer : public QObject {
    Q_OBJECT

public:
    explicit MockOllamaServer(QObject* parent = nullptr)
        : QObject(parent), m_server(new QTcpServer(this))
    {
        connect(m_server, &QTcpServer::newConnection, this, &MockOllamaServer::onNewConnection);
        m_server->listen(QHostAddress::LocalHost, 0);
    }

    ~MockOllamaServer() override
    {
        if (m_server && m_server->isListening()) {
            m_server->close();
        }
    }

    [[nodiscard]] uint16_t port() const { return m_server->serverPort(); }
    [[nodiscard]] std::string url() const { return "http://127.0.0.1:" + std::to_string(port()); }

    void reset()
    {
        m_statusCode = 200;
        m_statusText = "OK";
        m_responseBody.clear();
        m_contentType = "application/json";
        m_isStreaming = false;
        m_chunks.clear();
        m_delayMs = 0;
        m_requestCount = 0;
        m_lastRequestBody.clear();
    }

    void setResponse(int code, const std::string& statusText, const std::string& body,
                     const std::string& contentType = "application/json")
    {
        m_statusCode = code;
        m_statusText = statusText;
        m_responseBody = body;
        m_contentType = contentType;
        m_isStreaming = false;
        m_chunks.clear();
    }

    void setStreamingResponse(const std::vector<std::string>& chunks)
    {
        m_isStreaming = true;
        m_chunks = chunks;
    }

    void setDelay(int delayMs)
    {
        m_delayMs = delayMs;
    }

    int requestCount() const { return m_requestCount; }
    const std::string& lastRequestBody() const { return m_lastRequestBody; }

private slots:
    void onNewConnection()
    {
        while (m_server->hasPendingConnections()) {
            QTcpSocket* socket = m_server->nextPendingConnection();
            connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                m_lastRequestBody = socket->readAll().toStdString();
                m_requestCount++;
                if (m_delayMs > 0 && !m_isStreaming) {
                    QPointer<QTcpSocket> safeSocket = socket;
                    int delay = m_delayMs;
                    QTimer::singleShot(delay, this, [this, safeSocket]() {
                        if (safeSocket) {
                            sendNormal(safeSocket.data());
                        }
                    });
                } else if (m_isStreaming) {
                    sendStreaming(socket);
                } else {
                    sendNormal(socket);
                }
            });
        }
    }

private:
    void sendNormal(QTcpSocket* socket)
    {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
        QByteArray resp;
        resp.append(QString("HTTP/1.1 %1 %2\r\n").arg(m_statusCode).arg(QString::fromStdString(m_statusText)).toUtf8());
        resp.append(QString("Content-Type: %1\r\n").arg(QString::fromStdString(m_contentType)).toUtf8());
        resp.append(QString("Content-Length: %1\r\n").arg(m_responseBody.size()).toUtf8());
        resp.append("Connection: close\r\n\r\n");
        resp.append(QByteArray::fromStdString(m_responseBody));
        socket->write(resp);
        socket->flush();
        socket->disconnectFromHost();
    }

    void sendStreaming(QTcpSocket* socket)
    {
        if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
        QByteArray resp;
        resp.append("HTTP/1.1 200 OK\r\nContent-Type: application/x-ndjson\r\nConnection: close\r\n\r\n");
        for (const auto& chunk : m_chunks) {
            resp.append(QByteArray::fromStdString(chunk));
        }
        socket->write(resp);
        socket->flush();
        socket->disconnectFromHost();
    }

    QTcpServer* m_server{nullptr};
    int m_statusCode{200};
    std::string m_statusText{"OK"};
    std::string m_responseBody;
    std::string m_contentType{"application/json"};
    bool m_isStreaming{false};
    std::vector<std::string> m_chunks;
    int m_delayMs{0};
    int m_requestCount{0};
    std::string m_lastRequestBody;
};

// -----------------------------------------------------------------------------
// Temporary Database Guard for Test Isolation
// -----------------------------------------------------------------------------

class TempDbGuard {
public:
    TempDbGuard()
    {
        m_path = QDir::tempPath().toStdString() + "/flex_test_voice_db_" +
                 QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString() + ".db";
        QFile::remove(QString::fromStdString(m_path));
    }

    ~TempDbGuard()
    {
        QFile::remove(QString::fromStdString(m_path));
    }

    [[nodiscard]] const std::string& path() const noexcept { return m_path; }

private:
    std::string m_path;
};

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
    void setAutoFinish(bool autoFinish) { m_autoFinish = autoFinish; }

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
        if (m_autoFinish) {
            QTimer::singleShot(10, [this]() {
                notifyFinished();
            });
        }
        return true;
    }

    void stop() override
    {
        m_isSpeaking = false;
        m_stopCalledCount++;
        notifyStopped();
    }

    void notifyFinished()
    {
        m_isSpeaking = false;
        ITextToSpeechEngine::notifyFinished();
    }

    void notifyError(const std::string& msg)
    {
        m_isSpeaking = false;
        ITextToSpeechEngine::notifyError(msg);
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
    bool m_autoFinish{false};
    bool m_isSpeaking{false};
    int m_stopCalledCount{0};
    std::vector<std::string> m_spokenTexts;
};

// -----------------------------------------------------------------------------
// Mock Speech-to-Text Engine for Deterministic CI Testing
// -----------------------------------------------------------------------------

class MockSpeechToTextEngine : public flex::voice::ISpeechToTextEngine {
public:
    MockSpeechToTextEngine() = default;
    ~MockSpeechToTextEngine() override = default;

    bool loadModel(const std::string& modelPath) override
    {
        m_loadModelCalls++;
        m_lastModelPath = modelPath;
        if (m_failLoad) {
            m_loaded = false;
            return false;
        }
        m_loaded = true;
        return true;
    }

    [[nodiscard]] bool isModelLoaded() const override
    {
        return m_loaded;
    }

    void setModelLoaded(bool loaded)
    {
        m_loaded = loaded;
    }

    void setFailLoad(bool fail)
    {
        m_failLoad = fail;
    }

    void setFailTranscribe(bool fail, const std::string& errorMsg = "Simulated transcription failure")
    {
        m_failTranscribe = fail;
        m_transcribeErrorMessage = errorMsg;
    }

    void setTranscriptionText(const std::string& text)
    {
        m_mockText = text;
    }

    void setSimulateDelay(int delayMs)
    {
        m_delayMs = delayMs;
    }

    flex::voice::TranscriptionResult transcribe(const std::vector<float>& samples, int nThreads = 4) override
    {
        Q_UNUSED(nThreads);
        m_transcribeCalls++;
        m_lastSampleCount = samples.size();

        if (m_delayMs > 0) {
            int elapsed = 0;
            while (elapsed < m_delayMs && !m_cancelled.load()) {
                QThread::msleep(10);
                elapsed += 10;
            }
        }

        flex::voice::TranscriptionResult result;

        if (m_cancelled.load()) {
            result.error = flex::voice::SpeechToTextError::Cancelled;
            result.errorMessage = "Transcription cancelled.";
            return result;
        }

        if (m_failTranscribe) {
            result.error = flex::voice::SpeechToTextError::TranscriptionFailed;
            result.errorMessage = QString::fromStdString(m_transcribeErrorMessage);
            return result;
        }

        result.success = true;
        result.text = QString::fromStdString(m_mockText);
        result.inferenceDurationMs = 25;
        return result;
    }

    void cancel() override
    {
        m_cancelCalls++;
        m_cancelled.store(true);
    }

    void reset()
    {
        m_cancelled.store(false);
        m_transcribeCalls = 0;
        m_cancelCalls = 0;
        m_loadModelCalls = 0;
    }

    std::atomic<bool> m_cancelled{false};
    bool m_loaded{false};
    bool m_failLoad{false};
    bool m_failTranscribe{false};
    std::string m_transcribeErrorMessage;
    std::string m_mockText{"Testing FLEX voice recognition."};
    std::string m_lastModelPath;
    int m_delayMs{0};
    int m_loadModelCalls{0};
    int m_transcribeCalls{0};
    int m_cancelCalls{0};
    std::size_t m_lastSampleCount{0};
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

    // Regression check for Issue 2: intercept any Qt "No such method" warnings
    // during SapiWorker invocations (stopSpeech, initCom, speakText, checkIsSpeaking, cleanup).
    static bool s_sawNoSuchMethodWarning = false;
    s_sawNoSuchMethodWarning = false;
    auto prevHandler = qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& msg) {
        if (msg.contains("No such method")) {
            s_sawNoSuchMethodWarning = true;
            std::cerr << "MOC Warning intercepted: " << msg.toStdString() << "\n";
        }
    });

    flex::voice::TextToSpeechService service; // Constructs default SapiTtsEngine
    EXPECT_FALSE(service.isSpeaking());
    EXPECT_EQ(static_cast<int>(flex::voice::TtsState::Idle), static_cast<int>(service.state()));

    // Test SAPI stop on idle (invokes SapiWorker::stopSpeech)
    service.stop();
    EXPECT_FALSE(s_sawNoSuchMethodWarning);

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

    qInstallMessageHandler(prevHandler);
    EXPECT_FALSE(s_sawNoSuchMethodWarning);
}
#endif

// -----------------------------------------------------------------------------
// Speech-to-Text (STT) Tests (Stage 6B - Step 2)
// -----------------------------------------------------------------------------

void test_stt_valid_audio_input_and_transcription()
{
    std::cout << "[Test 19] STT valid audio input and transcription pipeline...\n";
    auto mockEngine = std::make_shared<MockSpeechToTextEngine>();
    mockEngine->setModelLoaded(true);
    mockEngine->setTranscriptionText("Testing FLEX voice recognition.");

    flex::voice::SpeechToTextService service(mockEngine);
    EXPECT_FALSE(service.isBusy());
    EXPECT_TRUE(service.isModelLoaded());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Idle), static_cast<int>(service.state()));

    flex::voice::AudioBuffer buf;
    buf.sampleRate = 16000;
    buf.channelCount = 1;
    buf.sampleSizeBits = 16;
    buf.durationMs = 1000;
    buf.samples.resize(16000, 0.25f);

    bool startedEmitted = false;
    bool finishedEmitted = false;
    QString resultText;
    int64_t reportedDuration = 0;

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(3000);

    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&service, &flex::voice::SpeechToTextService::transcriptionStarted, [&]() {
        startedEmitted = true;
    });
    QObject::connect(&service, &flex::voice::SpeechToTextService::transcriptionFinished,
                     [&](const QString& text, int64_t durationMs) {
        finishedEmitted = true;
        resultText = text;
        reportedDuration = durationMs;
        loop.quit();
    });

    timeoutTimer.start();
    bool dispatched = service.transcribeAsync(buf);
    EXPECT_TRUE(dispatched);
    EXPECT_TRUE(service.isBusy());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Transcribing), static_cast<int>(service.state()));

    loop.exec();

    EXPECT_TRUE(startedEmitted);
    EXPECT_TRUE(finishedEmitted);
    EXPECT_STR_EQ("Testing FLEX voice recognition.", resultText.toStdString());
    EXPECT_EQ(1000, reportedDuration);
    EXPECT_FALSE(service.isBusy());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Idle), static_cast<int>(service.state()));
    EXPECT_EQ(1, mockEngine->m_transcribeCalls);
}

void test_stt_empty_audio_rejection()
{
    std::cout << "[Test 20] STT empty audio rejection...\n";
    auto mockEngine = std::make_shared<MockSpeechToTextEngine>();
    mockEngine->setModelLoaded(true);

    flex::voice::SpeechToTextService service(mockEngine);

    flex::voice::AudioBuffer emptyBuf;
    emptyBuf.sampleRate = 16000;
    emptyBuf.channelCount = 1;
    emptyBuf.durationMs = 0;
    emptyBuf.samples.clear();

    bool errorEmitted = false;
    flex::voice::SpeechToTextError emittedErr = flex::voice::SpeechToTextError::None;
    QObject::connect(&service, &flex::voice::SpeechToTextService::transcriptionError,
                     [&](flex::voice::SpeechToTextError err, const QString&) {
        errorEmitted = true;
        emittedErr = err;
    });

    bool dispatched = service.transcribeAsync(emptyBuf);
    EXPECT_FALSE(dispatched);
    EXPECT_FALSE(service.isBusy());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Error), static_cast<int>(service.state()));
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::InvalidAudioInput), static_cast<int>(service.lastError()));
    EXPECT_TRUE(errorEmitted);
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::InvalidAudioInput), static_cast<int>(emittedErr));
    EXPECT_EQ(0, mockEngine->m_transcribeCalls);
}

void test_stt_invalid_sample_rate_and_channel_rejection()
{
    std::cout << "[Test 21] STT invalid sample rate and channel count rejection...\n";
    auto mockEngine = std::make_shared<MockSpeechToTextEngine>();
    mockEngine->setModelLoaded(true);

    flex::voice::SpeechToTextService service(mockEngine);

    // Reject non-16000 Hz
    flex::voice::AudioBuffer badRateBuf;
    badRateBuf.sampleRate = 44100;
    badRateBuf.channelCount = 1;
    badRateBuf.durationMs = 1000;
    badRateBuf.samples.resize(44100, 0.1f);

    bool okRate = service.transcribeAsync(badRateBuf);
    EXPECT_FALSE(okRate);
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::InvalidAudioInput), static_cast<int>(service.lastError()));

    // Reject multi-channel (non-mono)
    flex::voice::AudioBuffer badChannelBuf;
    badChannelBuf.sampleRate = 16000;
    badChannelBuf.channelCount = 2;
    badChannelBuf.durationMs = 1000;
    badChannelBuf.samples.resize(32000, 0.1f);

    bool okChannels = service.transcribeAsync(badChannelBuf);
    EXPECT_FALSE(okChannels);
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::InvalidAudioInput), static_cast<int>(service.lastError()));
    EXPECT_EQ(0, mockEngine->m_transcribeCalls);
}

void test_stt_nonfinite_and_out_of_range_rejection()
{
    std::cout << "[Test 22] STT non-finite and out-of-range sample rejection...\n";
    auto mockEngine = std::make_shared<MockSpeechToTextEngine>();
    mockEngine->setModelLoaded(true);

    flex::voice::SpeechToTextService service(mockEngine);

    // NaN rejection
    flex::voice::AudioBuffer nanBuf;
    nanBuf.sampleRate = 16000;
    nanBuf.channelCount = 1;
    nanBuf.durationMs = 100;
    nanBuf.samples.resize(1600, 0.1f);
    nanBuf.samples[100] = std::numeric_limits<float>::quiet_NaN();

    bool okNan = service.transcribeAsync(nanBuf);
    EXPECT_FALSE(okNan);
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::InvalidAudioInput), static_cast<int>(service.lastError()));

    // Inf rejection
    flex::voice::AudioBuffer infBuf;
    infBuf.sampleRate = 16000;
    infBuf.channelCount = 1;
    infBuf.durationMs = 100;
    infBuf.samples.resize(1600, 0.1f);
    infBuf.samples[100] = std::numeric_limits<float>::infinity();

    bool okInf = service.transcribeAsync(infBuf);
    EXPECT_FALSE(okInf);
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::InvalidAudioInput), static_cast<int>(service.lastError()));

    // Out-of-range rejection (e.g. 50.0f when normalized audio is around [-1, 1])
    flex::voice::AudioBuffer rangeBuf;
    rangeBuf.sampleRate = 16000;
    rangeBuf.channelCount = 1;
    rangeBuf.durationMs = 100;
    rangeBuf.samples.resize(1600, 0.1f);
    rangeBuf.samples[100] = 50.0f;

    bool okRange = service.transcribeAsync(rangeBuf);
    EXPECT_FALSE(okRange);
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::InvalidAudioInput), static_cast<int>(service.lastError()));
    EXPECT_EQ(0, mockEngine->m_transcribeCalls);
}

void test_stt_missing_model_file_and_load_failure()
{
    std::cout << "[Test 23] STT missing model file and load failure...\n";
    auto mockEngine = std::make_shared<MockSpeechToTextEngine>();
    mockEngine->setModelLoaded(false);

    flex::voice::SpeechToTextService service(mockEngine);
    EXPECT_FALSE(service.isModelLoaded());

    // 1. Non-existent file path rejection
    bool loadOk = service.loadModel("C:/definitely/non_existent_whisper_weights.bin");
    EXPECT_FALSE(loadOk);
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::ModelNotFound), static_cast<int>(service.lastError()));
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Error), static_cast<int>(service.state()));

    // 2. Transcribe without model loaded rejection
    flex::voice::AudioBuffer validBuf;
    validBuf.sampleRate = 16000;
    validBuf.channelCount = 1;
    validBuf.durationMs = 100;
    validBuf.samples.resize(1600, 0.1f);

    bool transcribeOk = service.transcribeAsync(validBuf);
    EXPECT_FALSE(transcribeOk);
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::ModelNotFound), static_cast<int>(service.lastError()));

    // 3. Engine loadModel failure with a dummy file > 1 MB
    QTemporaryFile tempFile;
    EXPECT_TRUE(tempFile.open());
    QByteArray dummyData(1024 * 1024 + 16, '\0');
    tempFile.write(dummyData);
    tempFile.flush();
    tempFile.close();

    mockEngine->setFailLoad(true);

    QEventLoop loop;
    QTimer timeoutTimer;
    timeoutTimer.setSingleShot(true);
    timeoutTimer.setInterval(3000);
    QObject::connect(&timeoutTimer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&service, &flex::voice::SpeechToTextService::modelLoadFinished, &loop, &QEventLoop::quit);

    timeoutTimer.start();
    bool dispatched = service.loadModel(tempFile.fileName());
    EXPECT_TRUE(dispatched);
    EXPECT_TRUE(service.isBusy());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::LoadingModel), static_cast<int>(service.state()));

    loop.exec();

    EXPECT_FALSE(service.isModelLoaded());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::ModelLoadFailed), static_cast<int>(service.lastError()));
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Error), static_cast<int>(service.state()));

    // 4. Successful load with mock engine
    mockEngine->setFailLoad(false);
    timeoutTimer.start();
    dispatched = service.loadModel(tempFile.fileName());
    EXPECT_TRUE(dispatched);
    loop.exec();

    EXPECT_TRUE(service.isModelLoaded());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::None), static_cast<int>(service.lastError()));
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Idle), static_cast<int>(service.state()));
}

void test_stt_transcription_failure_handling()
{
    std::cout << "[Test 24] STT transcription failure reporting...\n";
    auto mockEngine = std::make_shared<MockSpeechToTextEngine>();
    mockEngine->setModelLoaded(true);
    mockEngine->setFailTranscribe(true, "Simulated whisper context error");

    flex::voice::SpeechToTextService service(mockEngine);

    flex::voice::AudioBuffer buf;
    buf.sampleRate = 16000;
    buf.channelCount = 1;
    buf.durationMs = 500;
    buf.samples.resize(8000, 0.1f);

    bool errorEmitted = false;
    flex::voice::SpeechToTextError capturedErr = flex::voice::SpeechToTextError::None;
    QString capturedMsg;

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(3000);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&service, &flex::voice::SpeechToTextService::transcriptionError,
                     [&](flex::voice::SpeechToTextError err, const QString& msg) {
        errorEmitted = true;
        capturedErr = err;
        capturedMsg = msg;
        loop.quit();
    });

    timer.start();
    bool dispatched = service.transcribeAsync(buf);
    EXPECT_TRUE(dispatched);

    loop.exec();

    EXPECT_TRUE(errorEmitted);
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::TranscriptionFailed), static_cast<int>(capturedErr));
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::TranscriptionFailed), static_cast<int>(service.lastError()));
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Error), static_cast<int>(service.state()));
    EXPECT_FALSE(service.isBusy());
}

void test_stt_cancellation_handling()
{
    std::cout << "[Test 25] STT cancellation handling...\n";
    auto mockEngine = std::make_shared<MockSpeechToTextEngine>();
    mockEngine->setModelLoaded(true);
    mockEngine->setSimulateDelay(500); // 500ms delay to allow cancellation

    flex::voice::SpeechToTextService service(mockEngine);

    flex::voice::AudioBuffer buf;
    buf.sampleRate = 16000;
    buf.channelCount = 1;
    buf.durationMs = 1000;
    buf.samples.resize(16000, 0.1f);

    bool cancelledEmitted = false;
    QObject::connect(&service, &flex::voice::SpeechToTextService::transcriptionCancelled, [&]() {
        cancelledEmitted = true;
    });

    bool dispatched = service.transcribeAsync(buf);
    EXPECT_TRUE(dispatched);
    EXPECT_TRUE(service.isBusy());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Transcribing), static_cast<int>(service.state()));

    // Cancel mid-transcription
    service.cancel();

    EXPECT_FALSE(service.isBusy());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Idle), static_cast<int>(service.state()));
    EXPECT_TRUE(cancelledEmitted);
    EXPECT_TRUE(mockEngine->m_cancelled.load());
}

void test_stt_repeated_requests_and_busy_state()
{
    std::cout << "[Test 26] STT repeated requests and busy state guard...\n";
    auto mockEngine = std::make_shared<MockSpeechToTextEngine>();
    mockEngine->setModelLoaded(true);
    mockEngine->setSimulateDelay(300);

    flex::voice::SpeechToTextService service(mockEngine);

    flex::voice::AudioBuffer buf;
    buf.sampleRate = 16000;
    buf.channelCount = 1;
    buf.durationMs = 500;
    buf.samples.resize(8000, 0.1f);

    bool ok1 = service.transcribeAsync(buf);
    EXPECT_TRUE(ok1);
    EXPECT_TRUE(service.isBusy());

    // Immediate second call while busy must be rejected
    bool ok2 = service.transcribeAsync(buf);
    EXPECT_FALSE(ok2);
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextError::Busy), static_cast<int>(service.lastError()));

    // Model load while busy must also be rejected
    bool okLoad = service.loadModel("C:/dummy_model.bin");
    EXPECT_FALSE(okLoad);

    // Wait for the first transcription to complete
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(2000);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&service, &flex::voice::SpeechToTextService::transcriptionFinished, &loop, &QEventLoop::quit);
    timer.start();
    loop.exec();

    EXPECT_FALSE(service.isBusy());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Idle), static_cast<int>(service.state()));
}

void test_stt_safe_shutdown_with_pending_work()
{
    std::cout << "[Test 27] STT safe shutdown with pending asynchronous work...\n";
    auto mockEngine = std::make_shared<MockSpeechToTextEngine>();
    mockEngine->setModelLoaded(true);
    mockEngine->setSimulateDelay(1000);

    flex::voice::AudioBuffer buf;
    buf.sampleRate = 16000;
    buf.channelCount = 1;
    buf.durationMs = 1000;
    buf.samples.resize(16000, 0.1f);

    {
        auto service = std::make_unique<flex::voice::SpeechToTextService>(mockEngine);
        bool ok = service->transcribeAsync(buf);
        EXPECT_TRUE(ok);
        EXPECT_TRUE(service->isBusy());
        // Destruction occurs here while worker is actively in transcribe()
        service.reset();
    }

    // Engine cancellation should have been triggered during destructor cleanup
    EXPECT_TRUE(mockEngine->m_cancelled.load());
    EXPECT_TRUE(mockEngine->m_cancelCalls >= 1);
}

void test_stt_empty_transcription_output()
{
    std::cout << "[Test 28] STT empty transcription output handling...\n";
    auto mockEngine = std::make_shared<MockSpeechToTextEngine>();
    mockEngine->setModelLoaded(true);
    mockEngine->setTranscriptionText(""); // Empty string (e.g. silence in input)

    flex::voice::SpeechToTextService service(mockEngine);

    flex::voice::AudioBuffer buf;
    buf.sampleRate = 16000;
    buf.channelCount = 1;
    buf.durationMs = 500;
    buf.samples.resize(8000, 0.0f); // pure silence

    bool finishedEmitted = false;
    QString resultText("non-empty initial");

    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    timer.setInterval(3000);
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&service, &flex::voice::SpeechToTextService::transcriptionFinished,
                     [&](const QString& text, int64_t) {
        finishedEmitted = true;
        resultText = text;
        loop.quit();
    });

    timer.start();
    bool ok = service.transcribeAsync(buf);
    EXPECT_TRUE(ok);

    loop.exec();

    EXPECT_TRUE(finishedEmitted);
    EXPECT_TRUE(resultText.isEmpty());
    EXPECT_FALSE(service.isBusy());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Idle), static_cast<int>(service.state()));
}

void test_stt_real_model_smoke_test()
{
    std::cout << "[Test 29] STT real model and inference smoke test...\n";

    QString modelPath = QString::fromUtf8(qgetenv("FLEX_WHISPER_MODEL_PATH"));
    if (modelPath.isEmpty() || !QFileInfo::exists(modelPath)) {
        modelPath = QDir::homePath() + "/.flex/models/whisper/ggml-tiny.en.bin";
    }

    if (!QFileInfo::exists(modelPath)) {
        std::cout << "  [SKIP] No local Whisper model found at " << modelPath.toStdString() << "\n";
        EXPECT_TRUE(true);
        return;
    }

    std::cout << "  [INFO] Found local Whisper model: " << modelPath.toStdString() << "\n";

    // Use default WhisperCppEngine
    flex::voice::SpeechToTextService service;

    QEventLoop loadLoop;
    QTimer loadTimer;
    loadTimer.setSingleShot(true);
    loadTimer.setInterval(10000);
    QObject::connect(&loadTimer, &QTimer::timeout, &loadLoop, &QEventLoop::quit);
    QObject::connect(&service, &flex::voice::SpeechToTextService::modelLoadFinished, &loadLoop, &QEventLoop::quit);

    loadTimer.start();
    bool dispatched = service.loadModel(modelPath);
    EXPECT_TRUE(dispatched);

    loadLoop.exec();

    EXPECT_TRUE(service.isModelLoaded());
    EXPECT_STR_EQ(modelPath.toStdString(), service.loadedModelPath().toStdString());

    // Execute quick inference on 1.0s of audio
    flex::voice::AudioBuffer testBuf;
    testBuf.sampleRate = 16000;
    testBuf.channelCount = 1;
    testBuf.sampleSizeBits = 16;
    testBuf.durationMs = 1000;
    testBuf.samples.resize(16000, 0.0f);

    QEventLoop infLoop;
    QTimer infTimer;
    infTimer.setSingleShot(true);
    infTimer.setInterval(20000); // 20s allowance for CPU neural inference
    QObject::connect(&infTimer, &QTimer::timeout, &infLoop, &QEventLoop::quit);
    QObject::connect(&service, &flex::voice::SpeechToTextService::transcriptionFinished, &infLoop, &QEventLoop::quit);
    QObject::connect(&service, &flex::voice::SpeechToTextService::transcriptionError, &infLoop, &QEventLoop::quit);

    infTimer.start();
    bool started = service.transcribeAsync(testBuf);
    EXPECT_TRUE(started);

    infLoop.exec();

    EXPECT_FALSE(service.isBusy());
    EXPECT_EQ(static_cast<int>(flex::voice::SpeechToTextState::Idle), static_cast<int>(service.state()));
    std::cout << "  [PASS] Real whisper.cpp engine loaded and inferred without error!\n";
}

// -----------------------------------------------------------------------------
// Voice Conversation Pipeline Tests (Tests 30 to 48)
// -----------------------------------------------------------------------------

inline bool waitForCondition(const std::function<bool()>& condition, int timeoutMs = 3000)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (condition()) {
            return true;
        }
        QThread::msleep(10);
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return condition();
}

struct VoiceTestContext {
    MockOllamaServer ollama;
    TempDbGuard tempDb;
    std::shared_ptr<flex::ConversationStorage> storage;
    std::shared_ptr<flex::OllamaClient> client;
    std::shared_ptr<flex::BackendCoordinator> coordinator;
    std::shared_ptr<MockAudioCaptureSource> mockCapture;
    std::shared_ptr<MockSpeechToTextEngine> mockStt;
    std::shared_ptr<MockTtsEngine> mockTts;
    std::shared_ptr<flex::voice::AudioCaptureService> captureService;
    std::shared_ptr<flex::voice::SpeechToTextService> sttService;
    std::shared_ptr<flex::voice::TextToSpeechService> ttsService;
    std::unique_ptr<flex::voice::VoiceConversationController> controller;

    VoiceTestContext()
    {
        storage = std::make_shared<flex::ConversationStorage>(tempDb.path());
        flex::OllamaConfig cfg;
        cfg.baseUrl = ollama.url();
        cfg.model = "qwen2.5-coder:3b";
        client = std::make_shared<flex::OllamaClient>(cfg);
        coordinator = std::make_shared<flex::BackendCoordinator>(storage, client);

        mockCapture = std::make_shared<MockAudioCaptureSource>();
        mockCapture->setSimulatedSamples(16000, 1000, 440.0f);

        mockStt = std::make_shared<MockSpeechToTextEngine>();
        mockStt->setModelLoaded(true);
        mockStt->setTranscriptionText("Hello, how are you today?");

        mockTts = std::make_shared<MockTtsEngine>();
        mockTts->setAutoFinish(true);

        captureService = std::make_shared<flex::voice::AudioCaptureService>(mockCapture);
        sttService = std::make_shared<flex::voice::SpeechToTextService>(mockStt);
        ttsService = std::make_shared<flex::voice::TextToSpeechService>(mockTts);

        controller = std::make_unique<flex::voice::VoiceConversationController>(
            coordinator, captureService, sttService, ttsService);
    }

    void setOllamaResponse(const std::string& reply)
    {
        ollama.setResponse(200, "OK", "{\n"
            "  \"model\": \"qwen2.5-coder:3b\",\n"
            "  \"message\": {\"role\": \"assistant\", \"content\": \"" + reply + "\"},\n"
            "  \"done\": true,\n"
            "  \"total_duration\": 100000000,\n"
            "  \"eval_count\": 10\n"
            "}");
    }

    ~VoiceTestContext()
    {
        if (controller) {
            controller->cancelAll();
            controller.reset();
        }
        coordinator.reset();
    }
};

void test_voice_controller_successful_recording_to_transcription()
{
    std::cout << "[Test 30] VoiceController: successful recording to transcription...\n";
    VoiceTestContext ctx;
    ctx.controller->setAutoSubmitEnabled(false);
    EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Idle), static_cast<int>(ctx.controller->state()));
    EXPECT_FALSE(ctx.controller->isBusy());

    QString readyTranscript;
    QObject::connect(ctx.controller.get(), &flex::voice::VoiceConversationController::transcriptReadyForReview,
                     [&](const QString& t) {
        readyTranscript = t;
    });

    bool started = ctx.controller->startRecording();
    EXPECT_TRUE(started);
    EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Recording), static_cast<int>(ctx.controller->state()));
    EXPECT_TRUE(ctx.controller->isBusy());
    EXPECT_TRUE(ctx.coordinator->coreSession().isProcessing());
    EXPECT_EQ(FLEX_OPERATION_VOICE, ctx.coordinator->coreSession().currentOperation());

    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
    }));
    EXPECT_STR_EQ("Hello, how are you today?", ctx.controller->lastTranscript().toStdString());
    EXPECT_STR_EQ("Hello, how are you today?", readyTranscript.toStdString());
    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
}

void test_voice_controller_empty_silent_transcription()
{
    std::cout << "[Test 31] VoiceController: empty/silent transcription handling...\n";
    VoiceTestContext ctx;
    ctx.mockStt->setTranscriptionText("   \n\t  ");

    QString errEmitted;
    QObject::connect(ctx.controller.get(), &flex::voice::VoiceConversationController::errorOccurred,
                     [&](const QString& e) {
        errEmitted = e;
    });

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Error;
    }));
    EXPECT_FALSE(errEmitted.isEmpty());
    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
    EXPECT_EQ(0, ctx.ollama.requestCount());
}

void test_voice_controller_missing_model()
{
    std::cout << "[Test 32] VoiceController: missing/unloadable Whisper model handling...\n";
    qputenv("FLEX_WHISPER_MODEL_PATH", "C:/nonexistent/whisper/model.bin");
    VoiceTestContext ctx;
    ctx.mockStt->setModelLoaded(false);

    QString capturedError;
    QObject::connect(ctx.controller.get(), &flex::voice::VoiceConversationController::errorOccurred,
                     [&](const QString& err) {
        capturedError = err;
    });

    bool started = ctx.controller->startRecording();
    EXPECT_TRUE(started);
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Error;
    }));
    EXPECT_FALSE(capturedError.isEmpty());
    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
    qunsetenv("FLEX_WHISPER_MODEL_PATH");
}

void test_voice_controller_stt_failure()
{
    std::cout << "[Test 33] VoiceController: speech-to-text engine error handling...\n";
    VoiceTestContext ctx;
    ctx.mockStt->setModelLoaded(true);
    ctx.mockStt->setFailTranscribe(true, "Simulated transcription engine failure");

    QString errEmitted;
    QObject::connect(ctx.controller.get(), &flex::voice::VoiceConversationController::errorOccurred,
                     [&](const QString& e) {
        errEmitted = e;
    });

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Error;
    }));
    EXPECT_FALSE(errEmitted.isEmpty());
    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
}

void test_voice_controller_transcript_review_and_editing()
{
    std::cout << "[Test 34] VoiceController: transcript review and editing...\n";
    VoiceTestContext ctx;
    ctx.setOllamaResponse("Here is the edited response.");
    ctx.controller->setAutoSubmitEnabled(false);

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
    }));
    EXPECT_STR_EQ("Hello, how are you today?", ctx.controller->lastTranscript().toStdString());

    bool sub = ctx.controller->submitTranscript("How is the weather today?");
    EXPECT_TRUE(sub);
    EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Thinking), static_cast<int>(ctx.controller->state()));
    EXPECT_STR_EQ("How is the weather today?", ctx.controller->lastTranscript().toStdString());

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Idle;
    }));
    EXPECT_STR_EQ("Here is the edited response.", ctx.controller->lastAssistantResponse().toStdString());
}

void test_voice_controller_exactly_one_backend_submission()
{
    std::cout << "[Test 35] VoiceController: exactly one backend submission per confirmed transcript...\n";
    VoiceTestContext ctx;
    ctx.setOllamaResponse("Single request response.");
    ctx.controller->setAutoSubmitEnabled(false);

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
    }));

    int countBefore = ctx.ollama.requestCount();
    bool sub = ctx.controller->submitTranscript();
    EXPECT_TRUE(sub);

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Idle;
    }));
    EXPECT_EQ(countBefore + 1, ctx.ollama.requestCount());
}

void test_voice_controller_prevent_duplicate_submissions()
{
    std::cout << "[Test 36] VoiceController: prevent duplicate submissions while thinking...\n";
    VoiceTestContext ctx;
    ctx.setOllamaResponse("Duplicate test response.");
    ctx.ollama.setDelay(200);
    ctx.controller->setAutoSubmitEnabled(false);

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
    }));

    bool sub1 = ctx.controller->submitTranscript();
    EXPECT_TRUE(sub1);
    EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Thinking), static_cast<int>(ctx.controller->state()));

    bool sub2 = ctx.controller->submitTranscript();
    EXPECT_FALSE(sub2);

    bool rec = ctx.controller->startRecording();
    EXPECT_FALSE(rec);

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Idle;
    }));
}

void test_voice_controller_backend_failure_preserves_transcript()
{
    std::cout << "[Test 37] VoiceController: backend failure preserves user transcript...\n";
    VoiceTestContext ctx;
    ctx.ollama.setResponse(500, "Internal Server Error", R"({"error": "daemon out of memory"})");
    ctx.controller->setAutoSubmitEnabled(false);

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
    }));

    QString savedTranscript = ctx.controller->lastTranscript();
    bool sub = ctx.controller->submitTranscript();
    EXPECT_TRUE(sub);

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Error;
    }));
    EXPECT_STR_EQ(savedTranscript.toStdString(), ctx.controller->lastTranscript().toStdString());
    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
}

void test_voice_controller_successful_chat_persistence()
{
    std::cout << "[Test 38] VoiceController: successful chat persistence to SQLite...\n";
    VoiceTestContext ctx;
    ctx.setOllamaResponse("Persistent assistant answer.");
    ctx.controller->setAutoSubmitEnabled(false);

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
    }));

    ctx.controller->submitTranscript("Query about persistence.");

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Idle;
    }));

    auto convs = ctx.storage->listConversations();
    EXPECT_FALSE(convs.empty());
    if (!convs.empty()) {
        auto msgs = ctx.storage->getMessages(convs[0].id);
        EXPECT_TRUE(msgs.size() >= 2);
        if (msgs.size() >= 2) {
            EXPECT_STR_EQ("Query about persistence.", msgs[msgs.size() - 2].content);
            EXPECT_STR_EQ("Persistent assistant answer.", msgs[msgs.size() - 1].content);
        }
    }
}

void test_voice_controller_tts_enabled_and_disabled()
{
    std::cout << "[Test 39] VoiceController: TTS enabled and disabled behavior...\n";

    // 1. TTS Enabled
    {
        VoiceTestContext ctx;
        ctx.controller->setVoiceOutputEnabled(true);
        ctx.setOllamaResponse("Voice output active.");
        ctx.controller->setAutoSubmitEnabled(true);

        ctx.controller->startRecording();
        ctx.controller->stopRecording();

        EXPECT_TRUE(waitForCondition([&]() {
            return !ctx.mockTts->spokenTexts().empty();
        }));
        EXPECT_EQ(1, static_cast<int>(ctx.mockTts->spokenTexts().size()));
        if (!ctx.mockTts->spokenTexts().empty()) {
            EXPECT_STR_EQ("Voice output active.", ctx.mockTts->spokenTexts()[0]);
        }
        EXPECT_TRUE(waitForCondition([&]() {
            return ctx.controller->state() == flex::voice::VoiceSessionState::Idle;
        }));
    }

    // 2. TTS Disabled
    {
        VoiceTestContext ctx2;
        ctx2.controller->setVoiceOutputEnabled(false);
        ctx2.setOllamaResponse("Silent response.");
        ctx2.controller->setAutoSubmitEnabled(true);

        ctx2.controller->startRecording();
        ctx2.controller->stopRecording();

        EXPECT_TRUE(waitForCondition([&]() {
            return ctx2.controller->state() == flex::voice::VoiceSessionState::Idle;
        }));
        EXPECT_EQ(0, static_cast<int>(ctx2.mockTts->spokenTexts().size()));
    }
}

void test_voice_controller_tts_failure_does_not_break_chat()
{
    std::cout << "[Test 40] VoiceController: TTS failure does not break chat persistence...\n";
    VoiceTestContext ctx;
    ctx.mockTts->setFailSpeak(true);
    ctx.controller->setVoiceOutputEnabled(true);
    ctx.setOllamaResponse("Chat completes even if TTS fails.");
    ctx.controller->setAutoSubmitEnabled(true);

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Idle;
    }));
    EXPECT_STR_EQ("Chat completes even if TTS fails.", ctx.controller->lastAssistantResponse().toStdString());

    auto convs = ctx.storage->listConversations();
    EXPECT_FALSE(convs.empty());
    if (!convs.empty()) {
        auto msgs = ctx.storage->getMessages(convs[0].id);
        EXPECT_TRUE(msgs.size() >= 2);
        if (msgs.size() >= 2) {
            EXPECT_STR_EQ("Chat completes even if TTS fails.", msgs.back().content);
        }
    }
}

void test_voice_controller_cancellation_during_recording()
{
    std::cout << "[Test 41] VoiceController: cancellation during recording...\n";
    VoiceTestContext ctx;
    ctx.controller->startRecording();
    EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Recording), static_cast<int>(ctx.controller->state()));
    EXPECT_TRUE(ctx.coordinator->coreSession().isProcessing());

    ctx.controller->cancelVoiceInput();

    EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Idle), static_cast<int>(ctx.controller->state()));
    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
    EXPECT_FALSE(ctx.mockCapture->isRecording());
}

void test_voice_controller_cancellation_during_transcription()
{
    std::cout << "[Test 42] VoiceController: cancellation during transcription...\n";
    VoiceTestContext ctx;
    ctx.mockStt->setSimulateDelay(1000);

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Transcribing), static_cast<int>(ctx.controller->state()));
    EXPECT_TRUE(ctx.coordinator->coreSession().isProcessing());

    ctx.controller->cancelVoiceInput();

    EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Idle), static_cast<int>(ctx.controller->state()));
    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
    EXPECT_TRUE(ctx.mockStt->m_cancelled.load());
}

void test_voice_controller_cancellation_during_chat()
{
    std::cout << "[Test 43] VoiceController: cancellation during chat generation...\n";
    VoiceTestContext ctx;
    ctx.ollama.setDelay(1000);
    ctx.setOllamaResponse("Slow reply");
    ctx.controller->setAutoSubmitEnabled(false);

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
    }));

    ctx.controller->submitTranscript();
    EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Thinking), static_cast<int>(ctx.controller->state()));

    ctx.controller->cancelAll();

    EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Idle), static_cast<int>(ctx.controller->state()));
    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
}

void test_voice_controller_repeated_start_stop()
{
    std::cout << "[Test 44] VoiceController: repeated start and stop cycles...\n";
    VoiceTestContext ctx;

    for (int i = 0; i < 3; ++i) {
        bool ok = ctx.controller->startRecording();
        EXPECT_TRUE(ok);
        ctx.controller->stopRecording();

        EXPECT_TRUE(waitForCondition([&]() {
            return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
        }));
        ctx.controller->cancelVoiceInput();
        EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Idle), static_cast<int>(ctx.controller->state()));
        EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
    }
}

void test_voice_controller_safe_shutdown_with_pending_work()
{
    std::cout << "[Test 45] VoiceController: safe shutdown with active async work...\n";
    std::shared_ptr<MockSpeechToTextEngine> capturedMockStt;

    {
        VoiceTestContext ctx;
        capturedMockStt = ctx.mockStt;
        ctx.mockStt->setSimulateDelay(1000);

        ctx.controller->startRecording();
        ctx.controller->stopRecording();

        EXPECT_EQ(static_cast<int>(flex::voice::VoiceSessionState::Transcribing), static_cast<int>(ctx.controller->state()));
    }

    EXPECT_TRUE(capturedMockStt->m_cancelled.load());
}

void test_voice_controller_c_core_lifecycle_integrity()
{
    std::cout << "[Test 46] VoiceController: C core lifecycle integrity...\n";
    VoiceTestContext ctx;
    ctx.setOllamaResponse("Lifecycle verified.");
    ctx.controller->setAutoSubmitEnabled(false);

    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());

    // 1. Voice operation begins on recording
    ctx.controller->startRecording();
    EXPECT_EQ(FLEX_OPERATION_VOICE, ctx.coordinator->coreSession().currentOperation());
    EXPECT_TRUE(ctx.coordinator->coreSession().isProcessing());

    // 2. Continues through transcription
    ctx.controller->stopRecording();
    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
    }));

    // 3. Completed when review starts
    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
    EXPECT_FALSE(ctx.coordinator->coreSession().isProcessing());

    // 4. Chat operation begins on submission
    ctx.controller->submitTranscript();
    EXPECT_EQ(FLEX_OPERATION_CHAT, ctx.coordinator->coreSession().currentOperation());
    EXPECT_TRUE(ctx.coordinator->coreSession().isProcessing());

    // 5. Cleanly returns to Idle after response
    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Idle;
    }));
    EXPECT_TRUE(ctx.coordinator->coreSession().isIdle());
    EXPECT_FALSE(ctx.coordinator->coreSession().isProcessing());
}

void test_voice_controller_typed_chat_parity()
{
    std::cout << "[Test 47] VoiceController: typed chat vs voice chat database parity...\n";
    VoiceTestContext ctx;
    ctx.setOllamaResponse("Parity response.");

    // 1. Direct typed chat
    flex::ChatRequest req;
    req.message = "Parity prompt";
    auto typedRes = ctx.coordinator->sendChatSync(req);
    EXPECT_TRUE(typedRes.success);

    // 2. Voice controller submitted chat
    ctx.controller->setAutoSubmitEnabled(false);
    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
    }));

    ctx.controller->submitTranscript("Parity prompt");
    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Idle;
    }));

    auto convs = ctx.storage->listConversations();
    EXPECT_FALSE(convs.empty());
    int total = 0;
    for (const auto& conv : convs) {
        auto m = ctx.storage->getMessages(conv.id);
        total += m.size();
        if (m.size() >= 2) {
            EXPECT_STR_EQ("user", m[0].role);
            EXPECT_STR_EQ("assistant", m[1].role);
            EXPECT_STR_EQ("Parity prompt", m[0].content);
            EXPECT_STR_EQ("Parity response.", m[1].content);
        }
    }
    EXPECT_TRUE(total >= 4);
}

void test_voice_controller_no_ui_blocking()
{
    std::cout << "[Test 48] VoiceController: asynchronous non-blocking event loop execution...\n";
    VoiceTestContext ctx;
    ctx.mockStt->setSimulateDelay(200);

    ctx.controller->startRecording();
    ctx.controller->stopRecording();

    int ticks = 0;
    QTimer timer;
    timer.setInterval(10);
    QObject::connect(&timer, &QTimer::timeout, [&]() { ticks++; });
    timer.start();

    EXPECT_TRUE(waitForCondition([&]() {
        return ctx.controller->state() == flex::voice::VoiceSessionState::Reviewing;
    }, 3000));

    timer.stop();
    EXPECT_TRUE(ticks > 3);
}

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

    test_stt_valid_audio_input_and_transcription();
    test_stt_empty_audio_rejection();
    test_stt_invalid_sample_rate_and_channel_rejection();
    test_stt_nonfinite_and_out_of_range_rejection();
    test_stt_missing_model_file_and_load_failure();
    test_stt_transcription_failure_handling();
    test_stt_cancellation_handling();
    test_stt_repeated_requests_and_busy_state();
    test_stt_safe_shutdown_with_pending_work();
    test_stt_empty_transcription_output();
    test_stt_real_model_smoke_test();

    test_voice_controller_successful_recording_to_transcription();
    test_voice_controller_empty_silent_transcription();
    test_voice_controller_missing_model();
    test_voice_controller_stt_failure();
    test_voice_controller_transcript_review_and_editing();
    test_voice_controller_exactly_one_backend_submission();
    test_voice_controller_prevent_duplicate_submissions();
    test_voice_controller_backend_failure_preserves_transcript();
    test_voice_controller_successful_chat_persistence();
    test_voice_controller_tts_enabled_and_disabled();
    test_voice_controller_tts_failure_does_not_break_chat();
    test_voice_controller_cancellation_during_recording();
    test_voice_controller_cancellation_during_transcription();
    test_voice_controller_cancellation_during_chat();
    test_voice_controller_repeated_start_stop();
    test_voice_controller_safe_shutdown_with_pending_work();
    test_voice_controller_c_core_lifecycle_integrity();
    test_voice_controller_typed_chat_parity();
    test_voice_controller_no_ui_blocking();

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

#include "test_voice.moc"
