#ifndef COMMANDDOCK_H
#define COMMANDDOCK_H

#include "SideTitleBar.h"

#include <QDockWidget>
#include <QJSEngine>
#include <QLineEdit>
#include <QMap>
#include <QTextBrowser>

class QLabel;

// [1] 출력용
class DockHistoryEdit : public QTextBrowser {
public:
    explicit DockHistoryEdit(QWidget *parent = nullptr) : QTextBrowser(parent) {}
    QSize sizeHint() const override { return QSize(-1, 40); }
};

// [2] 입력용
class CommandInputLine : public QLineEdit {
    Q_OBJECT
public:
    explicit CommandInputLine(QWidget *parent = nullptr);
    void addHistory(const QString &cmd);
    void setupCompleter(const QStringList &commands);
    void setToolActive(bool active);
    void setCompleterEnabled(bool enabled);
    bool isToolActive() const { return m_isToolActive; }

protected:
    void keyPressEvent(QKeyEvent *event) override;

private slots:
    void onTextChanged(const QString &text);
    void applyCompleter();
    void clearCompleter();

private:
    QStringList history;
    int historyIndex;
    QCompleter *m_completer;
    bool m_isToolActive = false;
    QStringList m_commands;
};

// [3] 메인 Dock
class CommandDock : public QDockWidget {
    Q_OBJECT

public:
    enum LogType { Info, Command, Warning, Error, Option, Calculation };

    explicit CommandDock(QWidget *parent = nullptr);
    ~CommandDock();

    void logMessage(const QString &msg, LogType type = Info);
    void addCommandHistory(const QString &cmd);
    void logOptions(const QString &msg, const QStringList &options);
    void loadAliasFile(const QString &filePath);
    void setPrompt(const QString &text);
    void showInteractivePrompt(const QString &msg, const QStringList &options);
    void injectKeyPress(QKeyEvent *event);

    QLineEdit *inputWidget() const { return inputLine; }
    QString getAlias(const QString &key);
    void setToolActive(bool active);

signals:
    void commandSignal(const QString &command);

public slots:
    void onCommandEntered();
    void setFilterEnabled(bool enabled);
    void onCloseClicked();
    void onFloatClicked();
    void onAnchorClicked(const QUrl &url);
    void onToggleHistory();
    void onPromptLinkClicked(const QString &link);

private:
    QWidget *containerWidget;
    QWidget *contentArea;

    DockHistoryEdit *historyArea;
    CommandInputLine *inputLine;

    SideTitleBar *leftTitleBar;
    SideTitleBar *bottomTitleBar;

    QToolButton *btnHistoryToggle;
    QLabel *promptLabel;
    QToolButton *btnClose;
    QToolButton *btnFloat;

    QMap<QString, QString> aliasMap;
    QStringList nativeCommands;
    QJSEngine jsEngine;

    void setupUi();
    void processBatchCommands(const QStringList &commands);
    QStringList getSupportedCommands();
    QString evaluateExpression(const QString &input);
    bool m_isInjecting = false;
};

#endif // COMMANDDOCK_H