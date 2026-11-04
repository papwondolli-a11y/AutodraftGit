#ifndef NOTIMPLEMENTEDCOMMAND_H
#define NOTIMPLEMENTEDCOMMAND_H

#include "commandbase.h"
#include "iappcontext.h"
#include <QString>

// ─────────────────────────────────────────────────────────────────────────
// NotImplementedCommand — 아직 실제 기능이 붙지 않은 메뉴 항목용 자리표시 명령
//
// "기계부품" 탭처럼 서브메뉴 뼈대(버튼)는 먼저 다 등록해두고, 실제 그리기
// 로직(Tool)은 항목별로 순차적으로 나중에 붙이고 싶을 때 사용한다.
// 버튼을 눌러도 아무 툴도 시작하지 않고, 커맨드독에 "준비중" 안내 메시지만
// 출력한다. 실제 기능이 완성되면 appcommandregistry.cpp 에서 이 명령 대신
// 해당 기능의 ToolCommand<...> 로 교체하면 된다.
// ─────────────────────────────────────────────────────────────────────────
class NotImplementedCommand : public CommandBase {
    Q_OBJECT
public:
    // featureName : 커맨드독 안내 메시지에 표시할 기능 이름 (예: "너트")
    explicit NotImplementedCommand(IAppContext *ctx, const QString &featureName,
                                   QObject *parent = nullptr);

    void execute() override;

private:
    IAppContext *m_ctx;         // 커맨드독 등 앱 컨텍스트에 접근하기 위한 포인터
    QString      m_featureName; // 안내 메시지에 표시할 기능 이름
};

#endif // NOTIMPLEMENTEDCOMMAND_H
