#ifndef COMMANDS_H
#define COMMANDS_H

#include "commandbase.h"
#include "iappcontext.h"
#include <QDebug>
#include <QMessageBox>

// 예시 1: 서브윈도우 생성 커맨드
// (실제 구현 시 MainWindow 포인터 등을 받아야 할 수 있습니다)
// class NewSubWindowCommand : public CommandBase {
//     Q_OBJECT
// public:
//     explicit NewSubWindowCommand(QObject* parent = nullptr) : CommandBase(parent) {}

//     void execute() override {
//         // 실제로는 MainWindow에 시그널을 보내거나 메서드를 호출해야 함
//         qDebug() << "Execute: Create New SubWindow";
//     }
// };

// 예시 2: 그리기 도구 커맨드 (Line, Circle 등 공용)
class DrawToolCommand : public CommandBase {
    Q_OBJECT
    QString toolName;
public:
    explicit DrawToolCommand(const QString& name,  IAppContext* ctx)
        : CommandBase(), toolName(name)
    {
        Q_UNUSED(ctx);
    }

    void execute() override {
        qDebug() << "Execute Draw Tool:" << toolName;
    }
};

#endif // COMMANDS_H
