//toolCommand -> startTool -> view(mouse) -> scene(mouse)
#ifndef TOOLCOMMAND_H
#define TOOLCOMMAND_H

#include "commandbase.h"
#include "iappcontext.h"
#include "selecttool.h"

#include <QDebug>
#include <type_traits>

// [장점] MainWindow, QMdiArea, DrawSubWindow 헤더가 필요 없습니다. (컴파일 속도 향상)

// 템플릿 클래스는 구현부를 헤더에 두어야 링크 에러가 나지 않습니다.
template <typename ToolType>
class ToolCommand : public CommandBase
{
public:
    // 생성자에서 MainWindow 대신 인터페이스(IAppContext)를 받습니다.
    explicit ToolCommand(IAppContext* ctx, QObject* parent = nullptr)
        : CommandBase(parent)
        , m_ctx(ctx)
    {
    }

    void execute() override
    {
        if (!m_ctx) return;

        // [최적화] SelectTool 요청이 들어오면 new 하지 않고 finishCurrentTool 호출
        if (std::is_same<ToolType, SelectTool>::value) {
            m_ctx->finishCurrentTool();
        }
        else {
            m_ctx->startTool(new ToolType());
        }
    }

private:
    IAppContext* m_ctx;
};

#endif // TOOLCOMMAND_H
