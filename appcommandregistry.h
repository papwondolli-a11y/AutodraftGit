#ifndef APPCOMMANDREGISTRY_H
#define APPCOMMANDREGISTRY_H

#include <QObject>

class CommandManager;
class MainWindow;
class IAppContext;

class AppCommandRegistry : public QObject {
  Q_OBJECT
public:
  explicit AppCommandRegistry(QObject *parent = nullptr);

  // 명령어를 일괄 등록하는 함수
  void registerAllCommands(CommandManager *mgr, IAppContext *ctx,
                           class CommandFactory &factory);

private:
  //    IAppContext* m_ctx;
};

#endif // APPCOMMANDREGISTRY_H
