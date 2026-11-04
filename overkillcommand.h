#ifndef OVERKILLCOMMAND_H
#define OVERKILLCOMMAND_H

#include "commandbase.h"
#include "iappcontext.h"
#include <QObject>

class OverkillCommand : public CommandBase {
  Q_OBJECT
public:
  explicit OverkillCommand(IAppContext *ctx, QObject *parent = nullptr);
  void execute() override;

private:
  IAppContext *m_ctx;
};

#endif // OVERKILLCOMMAND_H
