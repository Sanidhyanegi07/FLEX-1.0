#ifndef FLEX_UI_MAIN_WINDOW_HPP
#define FLEX_UI_MAIN_WINDOW_HPP

#include "flex/backend.hpp"
#include "flex/ui/chat_view.hpp"
#include "flex/ui/conversation_list_widget.hpp"
#include "flex/ui/memory_view.hpp"
#include "flex/ui/navigation_sidebar.hpp"
#include "flex/ui/projects_view.hpp"
#include "flex/ui/prototype_view.hpp"
#include "flex/ui/right_sidebar.hpp"
#include "flex/ui/settings_view.hpp"
#include "flex/ui/top_header_widget.hpp"
#include "flex/ui/voice_studio_view.hpp"
#include "flex/voice/voice_conversation_controller.hpp"

#include <QMainWindow>
#include <QPointer>
#include <QStackedWidget>
#include <QThread>
#include <QTimer>

#include <memory>

namespace flex::ui {

/**
 * Main application window for FLEX 1.0 Native Desktop AI Assistant.
 *
 * Restored 3-Column Architecture:
 * - Persistent Top Telemetry Header (FLEX 1.0 brand, C Core badge, Ollama badge, OP counter, Sidebar toggle).
 * - Left Navigation Sidebar (Brand icon, New Chat, 7 View Switches, Recent Chats, Local Profile).
 * - Central Dynamic Workspace Container (QStackedWidget with Home, Voice Studio, Memory,
 *   Knowledge Base, Projects & Files, Tools, Settings).
 * - Right Information Sidebar (User Profile Card, System Status Panel, Quick Actions, Tagline).
 */
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(
        std::shared_ptr<BackendCoordinator> coordinator = nullptr,
        std::shared_ptr<voice::VoiceConversationController> voiceController = nullptr,
        QWidget* parent = nullptr);

    ~MainWindow() override;

    [[nodiscard]] std::shared_ptr<BackendCoordinator> coordinator() const { return m_coordinator; }
    [[nodiscard]] std::shared_ptr<voice::VoiceConversationController> voiceController() const { return m_voiceController; }
    [[nodiscard]] ChatView* chatView() const { return m_chatView; }
    [[nodiscard]] ConversationListWidget* conversationListWidget() const;

public slots:
    void handleSendChat(const QString& promptText);
    void handleNewConversation();
    void handleSelectConversation(const QString& conversationId);
    void handleDeleteConversation(const QString& conversationId);
    void refreshConversations();
    void checkAiConnectivity();
    void switchView(const QString& viewKey);

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onChatFinished(uint64_t requestId, const flex::ChatResult& result);
    void onChatError(uint64_t requestId, const flex::BackendError& error);
    void onChatToken(uint64_t requestId, const QString& token);
    void onToggleRightSidebar();
    void onDiagnosticsRequested();

private:
    void setupUi();
    void connectCoordinatorSignals();
    void updateTelemetryBar(bool ollamaOnline, const QString& modelName = QString());
    void updateCoreStateDisplay();

    std::shared_ptr<BackendCoordinator> m_coordinator;
    std::shared_ptr<voice::VoiceConversationController> m_voiceController;

    // Shell Components
    TopHeaderWidget* m_topHeader{nullptr};
    NavigationSidebar* m_navSidebar{nullptr};
    QStackedWidget* m_viewStack{nullptr};
    RightSidebar* m_rightSidebar{nullptr};

    // Central Views
    ChatView* m_chatView{nullptr};
    VoiceStudioView* m_voiceStudioView{nullptr};
    MemoryView* m_memoryView{nullptr};
    PrototypeView* m_knowledgeView{nullptr};
    ProjectsView* m_projectsView{nullptr};
    PrototypeView* m_toolsView{nullptr};
    SettingsView* m_settingsView{nullptr};

    // Diagnostics / Operation telemetry
    QTimer m_healthCheckTimer;
    QPointer<QThread> m_healthCheckThread;
    bool m_isCheckingHealth{false};
    uint64_t m_currentRequestId{0};
    uint64_t m_opCount{0};
    bool m_lastOllamaOnline{false};
    QString m_lastOllamaModel;
};

} // namespace flex::ui

#endif // FLEX_UI_MAIN_WINDOW_HPP
