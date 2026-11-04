#include "customtitlebar.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QToolBar>
#include <QToolButton>
#include <QStyle>
#include <QPushButton>
#include <QMainWindow>

CustomTitleBar::CustomTitleBar(QWidget *parent) : QWidget(parent) {
    setAttribute(Qt::WA_StyledBackground, true);
    setFixedHeight(30);

    // 스타일시트 전체 제거됨 (ThemeManager에서 관리)

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 1. 타이틀 아이콘
    QLabel *titleIconLabel = new QLabel(this);
    QPixmap pixmap(":/icons/autodraft1.png");
    titleIconLabel->setFixedSize(30, 30);
    if (!pixmap.isNull()) {
        titleIconLabel->setPixmap(pixmap.scaled(24, 24, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    titleIconLabel->setAlignment(Qt::AlignCenter);
    // 특정 아이콘의 기본 배경이 필요하다면 ObjectName을 부여하여 관리
    titleIconLabel->setObjectName("TitleIconLabel");
    layout->addWidget(titleIconLabel);

    // 2. 타이틀 툴바 (저장, 출력 등)
    m_titleToolBar = new QToolBar(this);
    m_titleToolBar->setIconSize(QSize(24, 24));

    QAction *saveAction = m_titleToolBar->addAction(QIcon(":/icons/filesave.png"), tr("저장하기"));
    QAction *saveAsAction = m_titleToolBar->addAction(QIcon(":/icons/filesaveas.png"), tr("다른 이름으로 저장"));

    // 람다 대신 멤버 함수 포인터로 연결
    connect(saveAction, &QAction::triggered, this, &CustomTitleBar::onSaveClicked);
    connect(saveAsAction, &QAction::triggered, this, &CustomTitleBar::onSaveAsClicked);

    m_redoAction = m_titleToolBar->addAction(QIcon(":/icons/Redo1.png"), tr("Redo"));
    m_undoAction = m_titleToolBar->addAction(QIcon(":/icons/Undo1.png"), tr("Undo"));
    m_undoAction->setShortcut(QKeySequence::Undo);          // Ctrl+Z
    m_redoAction->setShortcut(QKeySequence::Redo);          // Ctrl+Y
    m_undoAction->setShortcutContext(Qt::ApplicationShortcut);
    m_redoAction->setShortcutContext(Qt::ApplicationShortcut);

    QAction *printAction = m_titleToolBar->addAction(QIcon(":icons/fileprint.png"), tr("출력하기"));
    connect(printAction, &QAction::triggered, this, &CustomTitleBar::onPrintClicked);
    layout->addWidget(m_titleToolBar);

    // 3. 중간 여백 및 제목
    layout->addStretch();
    QLabel *mainTitleLabel = new QLabel(tr("AutoDraft v1.2(26,10,22)"), this);
    mainTitleLabel->setObjectName("MainTitleLabel"); // ThemeManager에서 폰트 설정용
    mainTitleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(mainTitleLabel);
    layout->addStretch();

    QPushButton *minimizeButton = new QPushButton("_", this);
    m_maximizeButton = new QPushButton("□", this);
    QPushButton *closeButton = new QPushButton("X", this);

    minimizeButton->setObjectName("TitleMinButton");
    m_maximizeButton->setObjectName("TitleMaxButton");
    closeButton->setObjectName("TitleCloseButton");

    minimizeButton->setFixedSize(24, 24);
    m_maximizeButton->setFixedSize(24, 24);
    closeButton->setFixedSize(24, 24);

    connect(minimizeButton, &QPushButton::clicked, this, &CustomTitleBar::onMinimizeClicked);
    connect(m_maximizeButton, &QPushButton::clicked, this, &CustomTitleBar::onMaximizeClicked);
    connect(closeButton, &QPushButton::clicked, this, &CustomTitleBar::onCloseClicked);

    layout->addWidget(minimizeButton);
    layout->addWidget(m_maximizeButton);
    layout->addWidget(closeButton);
}

void CustomTitleBar::updateMaximizeButtonIcon(bool isMaximized) {
    if (m_maximizeButton) {
        // 최대화 상태일 때의 아이콘(겹친 창)도 텍스트로 표현
        m_maximizeButton->setText(isMaximized ? "🗗" : "□");
    }
}
// 람다 대체를 위한 슬롯 구현부
void CustomTitleBar::onSaveClicked() { emit saveRequested(); }
void CustomTitleBar::onSaveAsClicked() { emit saveAsRequested(); }
void CustomTitleBar::onPrintClicked() { emit printRequested(); }
void CustomTitleBar::onMinimizeClicked() { emit minimizeRequested(); }
void CustomTitleBar::onMaximizeClicked() { emit maximizeRequested(); }
void CustomTitleBar::onCloseClicked() { emit closeRequested(); }