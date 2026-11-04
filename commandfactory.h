#ifndef COMMANDFACTORY_H
#define COMMANDFACTORY_H

#include <QMap>
#include <QString>
#include <functional>

class CommandBase;
class IAppContext;

class CommandFactory {
public:
  typedef std::function<CommandBase *(IAppContext *)> CommandCreator;

  CommandFactory() = default;

  void registerCommand(const QString &type, CommandCreator creator);
  CommandBase *createCommand(const QString &type, IAppContext *ctx);

private:
  QMap<QString, CommandCreator> m_creators;
};

#endif // COMMANDFACTORY_H
