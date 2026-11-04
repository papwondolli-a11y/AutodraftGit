#ifndef COMMANDBASE_H
#define COMMANDBASE_H

#include <QString>
#include <QObject>

// 모든 명령어의 부모가 되는 추상 클래스
class CommandBase : public QObject {
    Q_OBJECT
public:
    explicit CommandBase(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~CommandBase() {}

    // 실제 동작을 수행할 순수 가상 함수
    virtual void execute() = 0;
};

#endif // COMMANDBASE_H
