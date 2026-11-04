#include "commandmanager.h"
#include "abstracttool.h" // [필수] Tool 접근
#include "appcontext.h"   // [필수] AppContext 접근
#include "commanddock.h"
#include "custommenubar.h"
#include "customtabwidget.h"
#include "drawsubwindow.h" // [필수] Active Window 접근
#include "mainwindow.h"

#include <QAction>
#include <QDebug>
#include <QHBoxLayout>
#include <QMenu>
#include <QPushButton>
#include <QToolButton>

CommandManager::CommandManager(IAppContext *ctx, CustomMenuBar *menuBar,
                               CustomTabWidget *tabWidget, QObject *parent)
    : QObject(parent), m_ctx(ctx), m_menuBar(menuBar), m_tabWidget(tabWidget) {}

void CommandManager::registerCommand(CommandBase *cmd, const CommandInfo &info,
                                     const QString &alias) {
  if (!cmd)
    return;

  // 관리 리스트 등록 (중복 체크)
  if (!m_allCommands.contains(cmd)) {
    m_allCommands.append(cmd);
  }

  cmd->setObjectName(info.id());

  // UI 액션 매핑
  QAction *action = nullptr;
  if (info.id().startsWith("menu.")) {
    if (m_menuBar)
      action = m_menuBar->addCommandAction(info);
  } else if (info.id().startsWith("tab.")) {
    if (m_tabWidget)
      action = m_tabWidget->addCommandAction(info);
  }

  if (action) {
    action->setObjectName(info.id());
    connect(action, &QAction::triggered, this,
            &CommandManager::onCommandTriggered);
    m_commandMap.insert(action, cmd);
  }

  // 텍스트 명령어용 맵에 등록 (id에서 "menu.", "tab." 첫 접두어만 제거)
  QString nativeName = info.id();
  if (nativeName.startsWith("menu.") || nativeName.startsWith("tab."))
    nativeName = nativeName.section('.', 1);
  m_stringCommandMap.insert(nativeName.toLower(), cmd);

  // 별칭(Alias)이 있으면 추가 등록
  if (!alias.isEmpty()) {
    m_stringCommandMap.insert(alias.toLower(), cmd);
    // 아이콘 클릭 히스토리·반복 실행에 쓸 사용자 이름 저장 (최초 등록만)
    if (!m_commandAliasMap.contains(cmd))
      m_commandAliasMap.insert(cmd, alias.toLower());
  }
}

// 공통 실행 슬롯
void CommandManager::onCommandTriggered() {
  // 시그널을 보낸 객체(Sender)를 찾음
  QObject *senderObj = sender();
  if (!senderObj)
    return;

  // 매핑된 커맨드가 있는지 확인하고 실행
  if (m_commandMap.contains(senderObj)) {
    CommandBase *cmd = m_commandMap.value(senderObj);
    if (cmd) {
      // [수정] 클릭한 명령어도 마지막 명령어로 저장하여
      // 스페이스/엔터 시 반복 가능하게 함
      // 사용자 친화적 alias 우선, 없으면 내부 ID에서 접두어만 제거
      QString id = cmd->objectName();
      if (id.startsWith("menu.") || id.startsWith("tab."))
        id = id.section('.', 1);
      id = id.toLower();
      QString histCmd = m_commandAliasMap.value(cmd, id);
      m_lastCommand = histCmd;

      // 클릭 시에도 커맨드 독에 로그를 남기고 히스토리에 등록
      if (m_ctx && m_ctx->commandDock()) {
        m_ctx->commandDock()->logMessage(histCmd, CommandDock::Command);
        m_ctx->commandDock()->addCommandHistory(histCmd);
      }

      cmd->execute();
    }
  }
}

// [신규] 텍스트 명령어 실행
void CommandManager::executeStringCommand(const QString &fullCommand) {
  QString rawInput = fullCommand.trimmed();
  QString cmdToExecute = rawInput;

  // 1. 현재 실행 중인 툴이 있다면 원본 입력(엔터 포함)을 먼저 전달
  if (m_ctx) {
    DrawSubWindow *dw = m_ctx->activeDrawWindow();
    if (dw && dw->tool()) {
      if (dw->tool()->processCommand(rawInput)) {
        return; // 툴이 엔터나 특정 입력을 처리함
      }

      // [신규] 툴이 활성화 중인데 빈 입력(엔터)이 들어온 경우,
      // processCommand에서 처리하지 못했다면 keyPress로도 기회를 줌.
      // 단, SelectTool(IDLE 상태)인 경우에는 마지막 명령 반복을 위해 건너뜀.
      if (rawInput.isEmpty()) {
        bool isSelectTool = (QString(dw->tool()->metaObject()->className()) == "SelectTool");
        if (!isSelectTool) {
          QKeyEvent enterEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
          dw->tool()->keyPress(&enterEvent);
          return; 
        }
      }
    }
  }

  // 2. 툴이 처리하지 않았고 입력이 비어있으면 -> 마지막 명령어 반복
  if (rawInput.isEmpty()) {
    if (!m_lastCommand.isEmpty()) {
 //     qDebug() << "Repeat Last Command:" << m_lastCommand;
      cmdToExecute = m_lastCommand;

      if (m_ctx && m_ctx->commandDock()) {
        m_ctx->commandDock()->logMessage(cmdToExecute, CommandDock::Command);
      }
    } else {
      return; // 반복할 명령 없음
    }
  }

  // 3. 입력 파싱
  QStringList parts = cmdToExecute.split(' ', Qt::SkipEmptyParts);
  if (parts.isEmpty())
    return;

  QString cmdKey = parts.first().toLower();

  // 4. [중요] 반복된 명령어나 새로 입력된 명령어가 현재 툴의 서브커맨드(예:
  // Close)인지 다시 확인
  if (m_ctx) {
    DrawSubWindow *dw = m_ctx->activeDrawWindow();
    if (dw && dw->tool()) {
      if (dw->tool()->processCommand(cmdToExecute)) {
        return;
      }
    }
  }

  m_lastCommand = cmdToExecute;

  // 5. 별칭(Alias) 변환 수행
  QString resolvedCommand = cmdKey;
  if (m_ctx && m_ctx->commandDock()) {
    resolvedCommand = m_ctx->commandDock()->getAlias(cmdKey);
  }

  // 6. 전역 명령어 실행
  if (m_stringCommandMap.contains(resolvedCommand)) {
    CommandBase *cmd = m_stringCommandMap.value(resolvedCommand);
    if (cmd) {
      cmd->execute();
    }
  } else {
    // 모르는 명령어
    if (m_ctx && m_ctx->commandDock()) {
      m_ctx->commandDock()->logMessage(
          QString("Unknown command: %1").arg(cmdKey), CommandDock::Error);
    }
  }
}

CommandBase *CommandManager::findCommand(const QString &key) {
  if (key.isEmpty())
    return nullptr;

  QString searchKey = key.toLower().trimmed();

  // 1. Alias 맵에서 먼저 검색
  if (m_stringCommandMap.contains(searchKey)) {
    return m_stringCommandMap.value(searchKey);
  }

  // 2. 관리 리스트에서 objectName(ID)으로 검색
  for (int i = 0; i < m_allCommands.size(); ++i) {
    CommandBase *cmd = m_allCommands.at(i);
    if (cmd && cmd->objectName() == key) {
      return cmd;
    }
  }

  return nullptr;
}

CommandManager::~CommandManager() {
  // 중앙 관리 리스트에서 모든 커맨드 객체 일괄 삭제
  qDeleteAll(m_allCommands);
  m_allCommands.clear();

  m_commandMap.clear();
  m_stringCommandMap.clear();
}
