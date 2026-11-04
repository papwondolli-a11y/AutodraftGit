#ifndef COPYTOOL_H
#define COPYTOOL_H

#include "movetool.h"

// MoveTool을 상속받지만, 생성자에서 isCopyMode를 무조건 true로 설정하는 클래스
class CopyTool : public MoveTool
{
    Q_OBJECT
public:
    explicit CopyTool(QObject* parent = nullptr)
        : MoveTool(true, parent) // [핵심] 부모에게 true(복사모드)를 강제로 전달
    {
    }
};

#endif // COPYTOOL_H
