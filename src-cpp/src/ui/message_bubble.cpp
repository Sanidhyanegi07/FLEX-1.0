#include "flex/ui/message_bubble.hpp"
#include "flex/ui/theme.hpp"

#include <QHBoxLayout>
#include <QDateTime>
#include <QAbstractTextDocumentLayout>

namespace flex::ui {

MessageBubble::MessageBubble(
    const QString& role,
    const QString& content,
    const QString& timestamp,
    const QString& model,
    QWidget* parent)
    : QFrame(parent)
    , m_role(role.trimmed().toLower())
    , m_content(content)
    , m_timestamp(timestamp)
    , m_model(model)
{
    if (m_timestamp.isEmpty()) {
        m_timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    }
    setupUi();
}

void MessageBubble::setupUi()
{
    setFrameShape(QFrame::StyledPanel);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(14, 12, 14, 12);
    mainLayout->setSpacing(8);

    // Header layout
    auto* headerLayout = new QHBoxLayout();
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(8);

    m_roleBadge = new QLabel(this);
    m_roleBadge->setObjectName("RoleBadge");

    m_metaLabel = new QLabel(this);
    m_metaLabel->setObjectName("MetaLabel");
    m_metaLabel->setStyleSheet(QString("color: %1; font-size: 11px;").arg(Theme::TextMuted));

    QString metaText = m_timestamp;
    if (!m_model.isEmpty()) {
        metaText += QString(" • %1").arg(m_model);
    }
    m_metaLabel->setText(metaText);

    headerLayout->addWidget(m_roleBadge);
    headerLayout->addWidget(m_metaLabel);
    headerLayout->addStretch();

    mainLayout->addLayout(headerLayout);

    // Content Label (supports Markdown in Qt 6)
    m_contentBrowser = new QTextBrowser(this);
    m_contentBrowser->setReadOnly(true);
    m_contentBrowser->setFrameStyle(QFrame::NoFrame);
    m_contentBrowser->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentBrowser->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_contentBrowser->setStyleSheet("background-color: transparent; border: none; font-size: 13px;");
    m_contentBrowser->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

    connect(m_contentBrowser->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
            this, [this](const QSizeF& newSize) {
        m_contentBrowser->setFixedHeight(static_cast<int>(std::ceil(newSize.height())) + 6);
    });

    mainLayout->addWidget(m_contentBrowser);

    // Apply role-specific themes
    if (m_role == "user") {
        m_roleBadge->setText("USER");
        m_roleBadge->setStyleSheet(
            "background-color: #2D1A45; color: #C084FC; font-size: 10px; font-weight: bold; "
            "padding: 2px 7px; border-radius: 4px; border: 1px solid #7C3AED;"
        );
        setStyleSheet(QString(
            "QFrame { background-color: %1; border: 1px solid #2F2148; border-radius: 8px; margin-left: 40px; margin-right: 4px; }"
        ).arg(Theme::BgUserBubble));
    } else if (m_role == "assistant") {
        m_roleBadge->setText("FLEX AI");
        m_roleBadge->setStyleSheet(
            "background-color: #0E2938; color: #38BDF8; font-size: 10px; font-weight: bold; "
            "padding: 2px 7px; border-radius: 4px; border: 1px solid #0284C7;"
        );
        setStyleSheet(QString(
            "QFrame { background-color: %1; border: 1px solid %2; border-radius: 8px; margin-left: 4px; margin-right: 40px; }"
        ).arg(Theme::BgAssistantBubble, Theme::Border));
    } else { // System or Error
        m_roleBadge->setText(m_role.toUpper());
        m_roleBadge->setStyleSheet(
            "background-color: #381418; color: #F87171; font-size: 10px; font-weight: bold; "
            "padding: 2px 7px; border-radius: 4px; border: 1px solid #EF4444;"
        );
        setStyleSheet(
            "QFrame { background-color: #1A0F12; border: 1px solid #5C1D24; border-radius: 8px; margin: 4px; }"
        );
    }

    updateContentDisplay();
}

void MessageBubble::appendChunk(const QString& chunk)
{
    m_content.append(chunk);
    updateContentDisplay();
}

void MessageBubble::setContent(const QString& fullContent)
{
    m_content = fullContent;
    updateContentDisplay();
}

void MessageBubble::updateContentDisplay()
{
    if (!m_contentBrowser) {
        return;
    }
    m_contentBrowser->setMarkdown(m_content);
    // Refresh fixed height based on document content height
    int docHeight = static_cast<int>(std::ceil(m_contentBrowser->document()->size().height())) + 6;
    m_contentBrowser->setFixedHeight(std::max(28, docHeight));
}

} // namespace flex::ui

