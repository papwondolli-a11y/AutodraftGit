#ifndef NEWSUBWINDOWCOMMAND_H
#define NEWSUBWINDOWCOMMAND_H

#include "commandbase.h"
#include "iappcontext.h"


class MainWindow; // 전방 선언

class NewSubWindowCommand : public CommandBase
{
    Q_OBJECT
public:
    // 생성자에서 MainWindow 포인터를 받아옵니다.
    explicit NewSubWindowCommand(IAppContext* ctx, QObject* parent = nullptr);

    void execute() override;

private:
      IAppContext* m_ctx;
};

#endif // NEWSUBWINDOWCOMMAND_H
