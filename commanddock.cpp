#include "commanddock.h"
#include <QAbstractItemView>
#include <QApplication>
#include <QCoreApplication>
#include <QCompleter>
#include <QDebug>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QPointer>
#include <QSizePolicy>
#include <QStringList>
#include <QStyle>
#include <QTextStream>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>
#include <QStringListModel>

// =========================================================
// [CommandInputLine] 구현
// =========================================================
CommandInputLine::CommandInputLine(QWidget *parent)
    : QLineEdit(parent), historyIndex(0), m_completer(nullptr),
    m_isToolActive(false) {
    connect(this, &QLineEdit::textChanged, this,
            &CommandInputLine::onTextChanged);
}

void CommandInputLine::onTextChanged(const QString &text) {
    if (m_isToolActive && m_completer && this->completer() == m_completer) {
        if (m_completer->popup()) {
            if (text.length() <= 1) {
                QTimer::singleShot(0, m_completer->popup(), &QWidget::hide);
            }
        }
    }
}

void CommandInputLine::setupCompleter(const QStringList &commands) {
    m_commands = commands;

    if (!m_completer) {
        m_completer = new QCompleter(this);
        m_completer->setCaseSensitivity(Qt::CaseInsensitive);
        m_completer->setCompletionMode(QCompleter::PopupCompletion);

        QStringListModel *model = new QStringListModel(m_commands, m_completer);
        m_completer->setModel(model);

        QAbstractItemView *popup = m_completer->popup();
        if (popup) {
            popup->setFixedWidth(100);
        }
        this->setCompleter(m_completer);
    } else {
        if (QStringListModel *model = qobject_cast<QStringListModel *>(m_completer->model())) {
            model->setStringList(m_commands);
        }
    }
}

void CommandInputLine::setCompleterEnabled(bool enabled) {
    if (enabled) {
        applyCompleter();
    } else {
        clearCompleter();
    }
}

void CommandInputLine::applyCompleter() {
    if (m_completer) {
        if (QStringListModel *model = qobject_cast<QStringListModel *>(m_completer->model())) {
            model->setStringList(m_commands);
        }
    }
}

void CommandInputLine::clearCompleter() {
    if (m_completer) {
        if (m_completer->popup() && m_completer->popup()->isVisible()) {
            m_completer->popup()->hide();
        }
        if (QStringListModel *model = qobject_cast<QStringListModel *>(m_completer->model())) {
            model->setStringList(QStringList());
        }
    }
}

void CommandInputLine::setToolActive(bool active) { m_isToolActive = active; }

void CommandInputLine::addHistory(const QString &cmd) {
    if (!history.isEmpty() && history.last() == cmd) {
        historyIndex = history.size();
        return;
    }
    history.append(cmd);
    historyIndex = history.size();
}

void CommandInputLine::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Space && event->modifiers() == Qt::NoModifier) {
        // 빈 입력 + 대기 상태에서도 Enter로 변환 → onCommandEntered("")에서 직전 명령 반복
        {
            event->accept();
            if (m_completer && this->completer() == m_completer &&
                m_completer->popup()->isVisible()) {
                if (m_completer->popup()->currentIndex().isValid()) {
                    QString selectedText = m_completer->popup()->currentIndex().data().toString();
                    setText(selectedText);
                }
                m_completer->popup()->hide();
            }
            QKeyEvent enterEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
            QLineEdit::keyPressEvent(&enterEvent);
            return;
        }
    }

    if (m_completer && this->completer() == m_completer &&
        m_completer->popup()->isVisible()) {
        switch (event->key()) {
        case Qt::Key_Enter:
        case Qt::Key_Return:
            if (m_completer->popup()->currentIndex().isValid()) {
                QString text = m_completer->popup()->currentIndex().data().toString();
                setText(text);
            }
            m_completer->popup()->hide();
            break;
        case Qt::Key_Escape:
            event->ignore();
            return;
        }
    }

    if (event->key() == Qt::Key_Escape) {
        if (!text().isEmpty()) {
            clear();
        }
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Up) {
        if (historyIndex > 0) {
            historyIndex--;
            setText(history.at(historyIndex));
        }
        return;
    } else if (event->key() == Qt::Key_Down) {
        if (historyIndex < history.size()) {
            historyIndex++;
            if (historyIndex == history.size())
                clear();
            else
                setText(history.at(historyIndex));
        }
        return;
    }

    QLineEdit::keyPressEvent(event);
}

// =========================================================
// [CommandDock] 구현
// =========================================================
CommandDock::CommandDock(QWidget *parent) : QDockWidget(parent) {
    setAllowedAreas(Qt::AllDockWidgetAreas);
    setFeatures(QDockWidget::DockWidgetMovable |
                QDockWidget::DockWidgetFloatable |
                QDockWidget::DockWidgetClosable);

    QWidget *emptyTitle = new QWidget(this);
    setTitleBarWidget(emptyTitle);

    nativeCommands << "quit" << "batch" << "line" << "circle" << "undo" << "redo" << "u";

    setupUi();

    // 1순위: 배포 환경 — 실행 파일(.exe) 옆 data 폴더
    QString filePath = QCoreApplication::applicationDirPath() + "/data/Draft.alias";
    // 2순위: qmake 빌드 — exe가 release/ 또는 debug/ 하위일 때 한 단계 위 확인
    if (!QFile::exists(filePath)) {
        filePath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../data/Draft.alias");
    }
    // 3순위: CMake 개발 환경 — APP_SOURCE_PATH(소스 루트)에서 탐색
#ifdef APP_SOURCE_PATH
    if (!QFile::exists(filePath)) {
        filePath = QDir::cleanPath(APP_SOURCE_PATH) + QDir::separator() + "data/Draft.alias";
    }
#endif
    loadAliasFile(filePath);
}

CommandDock::~CommandDock() {}

void CommandDock::setToolActive(bool active) {
    if (inputLine)
        inputLine->setToolActive(active);
}

void CommandDock::setFilterEnabled(bool enabled) {
    if (inputLine)
        inputLine->setCompleterEnabled(enabled);
}

void CommandDock::onCommandEntered() {
    QString rawInput = inputLine->text().trimmed();
    if (rawInput.isEmpty() && !inputLine->isToolActive()) {
        emit commandSignal("");
        return;
    }

    inputLine->clear();
    inputLine->addHistory(rawInput);

    QString processedInput = rawInput;

    if (rawInput.contains(',')) {
        QStringList parts = rawInput.split(',');
        QStringList calcParts;
        for (int i = 0; i < parts.size(); ++i) {
            calcParts << evaluateExpression(parts.at(i).trimmed());
        }
        processedInput = calcParts.join(",");
    } else {
        processedInput = evaluateExpression(rawInput);
    }

    bool wasCalculated = (processedInput != rawInput);

    if (wasCalculated) {
        logMessage(QString("Calculated: %1 -> %2").arg(rawInput, processedInput),
                   Calculation);
    }

    logMessage(processedInput, Command);
    emit commandSignal(processedInput);
}

void CommandDock::setPrompt(const QString &text) {
    if (promptLabel)
        promptLabel->setText(text);
}

void CommandDock::logMessage(const QString &msg, LogType type) {
    QString color;
    QString prefix;

    switch (type) {
    case Command:
        color = "#4285f4"; // 블루 하이라이트 (어두운 배경에서도 잘 보임)
        prefix = "Cmd: ";
        break;
    case Warning:
        color = "#FF8800";
        prefix = "Warning: ";
        break;
    case Error:
        color = "#e81123";
        prefix = "Error: ";
        break;
    case Option:
        color = "#4285f4";
        prefix = "";
        break;
    case Calculation:
        color = "#008080";
        prefix = "";
        break;
    case Info:
    default:
        // [핵심 수정] 하드코딩 '#000000' 제거 -> 현재 위젯 테마 팔레트의 Text 색상을 동적으로 할당
        color = palette().color(QPalette::Text).name();
        prefix = "";
        break;
    }

    QString html =
        QString("<font color='%1'>%2%3</font>").arg(color, prefix, msg);

    historyArea->append(html);
    historyArea->moveCursor(QTextCursor::End);
}

void CommandDock::logOptions(const QString &msg, const QStringList &options) {
    // [핵심 수정] 하드코딩 'black' 제거 -> QPalette에서 테마 글자색상 가져옴
    QString themeColor = palette().color(QPalette::Text).name();
    QString html = QString("<font color='%1'>%2 </font>").arg(themeColor, msg);

    for (int i = 0; i < options.size(); ++i) {
        QString opt = options.at(i);
        // 링크 색상도 테마에 어울리도록 파란색 톤 조정
        html += QString("&nbsp;<a href='%1' style='color:#4285f4; "
                        "text-decoration:none;'>[%2]</a>")
                    .arg(opt, opt);
    }
    historyArea->append(html);
    historyArea->moveCursor(QTextCursor::End);
}

void CommandDock::addCommandHistory(const QString &cmd) {
    if (inputLine) {
        inputLine->addHistory(cmd);
    }
}

QString CommandDock::evaluateExpression(const QString &input) {
    bool isNumber;
    input.toDouble(&isNumber);
    if (isNumber)
        return input;
    QJSValue result = jsEngine.evaluate(input);
    if (result.isNumber())
        return QString::number(result.toNumber());
    return input;
}

void CommandDock::loadAliasFile(const QString &filePath) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (inputLine)
            inputLine->setupCompleter(getSupportedCommands());
        return;
    }
    aliasMap.clear();
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (line.isEmpty() || line.startsWith("#"))
            continue;
        QStringList parts = line.split('\t');
        if (parts.size() >= 2) {
            QString alias = parts[0].trimmed();
            QString command = parts[1].trimmed();
            if (!alias.isEmpty() && !command.isEmpty()) {
                aliasMap.insert(alias, command);
                if (!nativeCommands.contains(command))
                    nativeCommands.append(command);
            }
        }
    }
    file.close();
    if (inputLine)
        inputLine->setupCompleter(getSupportedCommands());
}

QStringList CommandDock::getSupportedCommands() {
    QStringList cmds = nativeCommands;
    cmds.append(aliasMap.keys());
    cmds.removeDuplicates();
    cmds.sort();
    return cmds;
}

void CommandDock::onAnchorClicked(const QUrl &url) {
    inputLine->setText(url.toString());
    onCommandEntered();
}

void CommandDock::setupUi() {
    containerWidget = new QWidget(this);
    containerWidget->setObjectName("CommandDockContainer");

    QHBoxLayout *rootLayout = new QHBoxLayout(containerWidget);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    leftTitleBar = new SideTitleBar(Qt::Vertical, containerWidget);
    leftTitleBar->setTargetDock(this);
    connect(leftTitleBar, &SideTitleBar::closeClicked, this,
            &CommandDock::onCloseClicked);
    connect(leftTitleBar, &SideTitleBar::floatClicked, this,
            &CommandDock::onFloatClicked);

    contentArea = new QWidget(containerWidget);
    contentArea->setObjectName("CommandDockContent");

    QVBoxLayout *contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(4, 4, 4, 4);
    contentLayout->setSpacing(4);

    historyArea = new DockHistoryEdit(contentArea);
    historyArea->setReadOnly(true);
    historyArea->setObjectName("CommandHistoryArea");
    historyArea->setMinimumHeight(40);
    historyArea->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    historyArea->setOpenExternalLinks(false);
    connect(historyArea, &QTextBrowser::anchorClicked, this,
            &CommandDock::onAnchorClicked);

    QHBoxLayout *inputLayout = new QHBoxLayout();
    inputLayout->setContentsMargins(0, 0, 0, 0);
    inputLayout->setSpacing(0);

    bottomTitleBar = new SideTitleBar(Qt::Horizontal, contentArea);
    bottomTitleBar->setTargetDock(this);
    bottomTitleBar->setVisible(false);

    connect(bottomTitleBar, &SideTitleBar::closeClicked, this,
            &CommandDock::onCloseClicked);
    connect(bottomTitleBar, &SideTitleBar::floatClicked, this,
            &CommandDock::onFloatClicked);

    promptLabel = new QLabel(tr(" Command : "), contentArea);
    promptLabel->setObjectName("CommandPromptLabel");
    promptLabel->setTextFormat(Qt::RichText);
    promptLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    promptLabel->setOpenExternalLinks(false);
    connect(promptLabel, &QLabel::linkActivated, this,
            &CommandDock::onPromptLinkClicked);

    inputLine = new CommandInputLine(contentArea);
    inputLine->setPlaceholderText(tr("  Type command..."));
    inputLine->setObjectName("CommandInputLine");

    btnHistoryToggle = new QToolButton(contentArea);
    btnHistoryToggle->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    btnHistoryToggle->setToolTip(tr("Hide History"));
    btnHistoryToggle->setFixedSize(20, 20);
    btnHistoryToggle->setObjectName("CommandToggleBtn");

    inputLayout->addWidget(bottomTitleBar);
    inputLayout->addWidget(promptLabel);
    inputLayout->addWidget(inputLine);
    inputLayout->addWidget(btnHistoryToggle);

    contentLayout->addWidget(historyArea);
    contentLayout->addLayout(inputLayout);

    rootLayout->addWidget(leftTitleBar);
    rootLayout->addWidget(contentArea);

    setWidget(containerWidget);

    connect(inputLine, &QLineEdit::returnPressed, this,
            &CommandDock::onCommandEntered);
    connect(btnHistoryToggle, &QToolButton::clicked, this,
            &CommandDock::onToggleHistory);
}

void CommandDock::showInteractivePrompt(const QString &msg,
                                        const QStringList &options) {
    QString historyText = msg;
    if (!options.isEmpty()) {
        historyText += " [" + options.join("/") + "]" + " : ";
    }
    logMessage(historyText, Info);

    // [핵심 수정] <font color='black'> 태그를 지우고 <span>으로 대체하여,
    // QSS의 @cLatext 색상이 QLabel 프롬프트에 자동으로 스며들도록 수정했습니다.
    QString html =
        QString("<span>%1 </span>").arg(msg.toHtmlEscaped());
    for (int i = 0; i < options.size(); ++i) {
        QString opt = options.at(i);
        html += QString("&nbsp;<a href='%1' style='color:#4285f4; "
                        "text-decoration:none;'>[%2] </a> ")
                    .arg(opt, opt);
    }
    promptLabel->setText(html);
}

void CommandDock::onPromptLinkClicked(const QString &link) {
    inputLine->setText(link);
    onCommandEntered();
}

void CommandDock::onToggleHistory() {
    bool isVisible = historyArea->isVisible();

    if (isVisible) {
        historyArea->setVisible(false);
        leftTitleBar->setVisible(false);

        if (bottomTitleBar)
            bottomTitleBar->setVisible(true);

        btnHistoryToggle->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
        btnHistoryToggle->setToolTip(tr("Show History"));

    } else {
        historyArea->setVisible(true);
        leftTitleBar->setVisible(true);

        if (bottomTitleBar)
            bottomTitleBar->setVisible(false);

        btnHistoryToggle->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
        btnHistoryToggle->setToolTip(tr("Hide History"));
        historyArea->moveCursor(QTextCursor::End);
    }
}

void CommandDock::injectKeyPress(QKeyEvent *event) {
    if (!inputLine || m_isInjecting)
        return;

    m_isInjecting = true;
    inputLine->setFocus();
    QApplication::sendEvent(inputLine, event);
    m_isInjecting = false;
}

QString CommandDock::getAlias(const QString &key) {
    if (aliasMap.contains(key)) {
        return aliasMap.value(key);
    }
    return key;
}

void CommandDock::onCloseClicked() { close(); }
void CommandDock::onFloatClicked() { setFloating(!isFloating()); }
void CommandDock::processBatchCommands(const QStringList &commands) {
    Q_UNUSED(commands)
}