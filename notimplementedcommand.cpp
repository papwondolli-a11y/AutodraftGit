#include "notimplementedcommand.h"
#include "commanddock.h"

// 생성자: 컨텍스트와 안내 메시지에 쓸 기능 이름을 저장해둔다.
NotImplementedCommand::NotImplementedCommand(IAppContext *ctx, const QString &featureName,
                                             QObject *parent)
    : CommandBase(parent), m_ctx(ctx), m_featureName(featureName)
{}

// execute(): 실제 그리기 툴을 시작하는 대신, 커맨드독에 "준비중" 경고 메시지만 출력한다.
void NotImplementedCommand::execute() {
    if (!m_ctx || !m_ctx->commandDock()) return;

    m_ctx->commandDock()->logMessage(
        tr("[%1] 기능은 아직 준비중입니다.").arg(m_featureName),
        CommandDock::Warning);
}
