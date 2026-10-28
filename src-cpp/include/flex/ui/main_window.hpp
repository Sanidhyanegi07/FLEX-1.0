#ifndef FLEX_UI_MAIN_WINDOW_HPP
#define FLEX_UI_MAIN_WINDOW_HPP

#include "flex/backend.hpp"
#include "flex/ui/chat_view.hpp"
#include "flex/ui/conversation_list_widget.hpp"

#include <QMainWindow>
#include <QLabel>
#include <QPushButton>
#include <QSplitter>
#include <QTimer>

#include <memory>

namespace flex::ui {

/**
 * Main application window for FLEX 1.0 Native Desktop AI Assistant.
 *
 * Responsibilities:
 * - Hosts the left navigation sidebar with conversation history and live telemetry.
 * - Hosts the primary conversation workspace (ChatView).
 * - Integrates directly with the native C++ BackendCoordinator.
 * - Manages asynchronous chat dispatches, streaming responses, and cancellation.
 * - Non-blocking background health check for local Ollama service.
 * - Safe lifecycle and shutdown handling.
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        std::shared_ptr<BackendCoordinator> coordinator = nullptr,
        QWidget* parent = nullptr);

    ~MainWindow() override;

    [[nodiscard]] std::shared_ptr<BackendCoordinator> coordinator() const { return m_coordinator; }
    [[nodiscard]] ChatView* chatView() const { return m_chatView; }
    [[nodiscard]] ConversationListWidget* conversationListWidget() const { return m_convList; }

public slots:
    void handleSendChat(const QString& promptText);
    void handleNewConversation();
    void handleSelectConversation(const QString& conversationId);
    void handleDeleteConversation(const QString& conversationId);
    void refreshConversations();
    void checkAiConnectivity();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onChatFinished(uint64_t requestId, const flex::ChatResult& result);
    void onChatError(uint64_t requestId, const flex::BackendError& error);
    void onChatToken(uint64_t requestId, const QString& token);

private:
    void setupUi();
    void connectCoordinatorSignals();
    void updateTelemetryBar(bool ollamaOnline, const QString& modelName = QString());
    void updateCoreStateDisplay();

    std::shared_ptr<BackendCoordinator> m_coordinator;

    // UI Widgets
    QSplitter* m_splitter{nullptr};
    QWidget* m_sidebarWidget{nullptr};
    QPushButton* m_newChatButton{nullptr};
    ConversationListWidget* m_convList{nullptr};
    ChatView* m_chatView{nullptr};

    // Telemetry labels
    QLabel* m_ollamaStatusLabel{nullptr};
    QLabel* m_coreStatusLabel{nullptr};
    QLabel* m_dbStatusLabel{nullptr};

    // Background timers
    QTimer m_healthCheckTimer;
    bool m_isCheckingHealth{false};
    uint64_t m_currentRequestId{0};
};

} // namespace flex::ui

#endif // FLEX_UI_MAIN_WINDOW_HPP

