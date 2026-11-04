#include "commandfactory.h"
#include "commandbase.h"
#include <QDebug>

void CommandFactory::registerCommand(const QString &type,
                                     CommandCreator creator) {
  m_creators[type] = creator;
}

CommandBase *CommandFactory::createCommand(const QString &type,
                                           IAppContext *ctx) {
  if (m_creators.contains(type)) {
    return m_creators[type](ctx);
  }
  qWarning() << "CommandFactory: Unknown command type" << type;
  return nullptr;
}
