#include "newsubwindowcommand.h"
#include "appcontext.h"
#include "mainwindow.h"
#include "drawsubwindow.h" // DrawSubWindow 생성을 위해 필요
#include "templatemanager.h"

NewSubWindowCommand::NewSubWindowCommand(IAppContext* ctx, QObject* parent)
    : CommandBase(parent), m_ctx(ctx)
{
}

void NewSubWindowCommand::execute()
{
    if (!m_ctx) return;

    // [QNEW] 기본 템플릿(acad.dwt 개념)이 지정돼 있으면 그것으로 시작한다.
    // 지정이 없거나 파일이 사라졌으면 defaultTemplatePath()가 빈 문자열을
    // 돌려주고, createWindowFromTemplate()는 종전과 동일한 "빈 도면"을 만든다.
    const QString tmpl = TemplateManager::defaultTemplatePath();
    // asPrototype=false : Ctrl+N 은 내용은 기본 템플릿을 따르되, 이름은 종전과
    // 같은 "Drawing N"을 유지한다 ("원형도면" 명명은 템플릿 보기>만들기 전용).
    TemplateManager::createWindowFromTemplate(m_ctx, tmpl, /*asPrototype=*/false);
}
