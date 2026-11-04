#ifndef COMMANDMANAGER_H
#define COMMANDMANAGER_H

#include "CommandInfo.h"
#include "commandbase.h"
#include <QMap>
#include <QObject>

class IAppContext;
class CustomMenuBar;
class CustomTabWidget;
class QAction;
class QAbstractButton;

class CommandManager : public QObject {
  Q_OBJECT
public:
  // 생성자에서 UI 컴포넌트들의 포인터와 컨텍스트를 받습니다.
  explicit CommandManager(IAppContext *ctx, CustomMenuBar *menuBar,
                          CustomTabWidget *tabWidget,
                          QObject *parent = nullptr);

  // 명령어 등록 함수 (Alias 포함 가능)
  void registerCommand(CommandBase *cmd, const CommandInfo &info,
                       const QString &alias = "");

  // [신규] 텍스트 명령어 실행 함수 (MainWindow가 호출함)
  void executeStringCommand(const QString &fullCommand);

  // [통합] ID 또는 별칭(Alias)으로 커맨드 객체 찾기
  CommandBase *findCommand(const QString &key);

  ~CommandManager();

private slots:
  // UI 액션 발생 시 커맨드를 실행하는 공통 슬롯
  void onCommandTriggered();

private:
  IAppContext *m_ctx;
  CustomMenuBar *m_menuBar;
  CustomTabWidget *m_tabWidget;

  // [신규] 모든 커맨드 객체를 관리하는 리스트 (메모리 해제 전용)
  QList<CommandBase *> m_allCommands;

  QMap<QObject *, CommandBase *> m_commandMap;
  // [신규] 텍스트용 (QString 키, 소문자 기준)
  QMap<QString, CommandBase *> m_stringCommandMap;
  // 커맨드 객체 → 사용자 친화적 alias 이름 (히스토리/반복 실행에 사용)
  QMap<CommandBase *, QString>  m_commandAliasMap;
  QString m_lastCommand; // [신규] 마지막 명령어 저장 변수
};

#endif // COMMANDMANAGER_H
