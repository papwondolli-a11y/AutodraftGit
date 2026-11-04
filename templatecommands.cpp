#include "templatecommands.h"

#include "drawsubwindow.h"
#include "filemanager.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "newfromtemplatedialog.h"
#include "templatemanager.h"

#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QMdiArea>
#include <QMessageBox>
#include <QWidget>

// ── NewFromTemplateCommand ──────────────────────────────────────────────────
NewFromTemplateCommand::NewFromTemplateCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void NewFromTemplateCommand::execute() {
    if (!m_ctx)
        return;

    QWidget *parent = m_ctx->activeDrawWindow();
    NewFromTemplateDialog dlg(m_ctx, parent);
    if (dlg.exec() != QDialog::Accepted)
        return;

    // [편집] 선택한 템플릿 파일 자체를 연다.
    if (dlg.selectedAction() == NewFromTemplateDialog::Action::Edit) {
        const QString path = dlg.selectedTemplatePath();
        if (TemplateManager::openTemplateForEdit(m_ctx, path))
            m_ctx->showStatusText(
                tr("템플릿 '%1' 편집 중 — 저장하면 원본 템플릿에 반영됩니다.")
                    .arg(QFileInfo(path).completeBaseName()));
        else
            m_ctx->showStatusText(tr("템플릿을 열지 못했습니다."));
        return;
    }

    // [만들기] 목록 선택과 무관하게 새로운 빈 원형도면을 만든다.
    if (dlg.selectedAction() == NewFromTemplateDialog::Action::CreateNew) {
        TemplateManager::createWindowFromTemplate(m_ctx, QString(),
                                                   /*asPrototype=*/true);
        m_ctx->showStatusText(tr("새 원형도면을 만들었습니다."));
        return;
    }

    // [교체] 새 창은 열지 않고, 선택한 항목을 기본 원형도면(Ctrl+N)으로만
    // 설정한다. 창을 만들지 않으므로 대시보드 상태 그대로 유지된다.
    if (dlg.selectedAction() == NewFromTemplateDialog::Action::SetDefault) {
        const QString path = dlg.selectedTemplatePath();
        TemplateManager::setDefaultTemplatePath(path);
        // 대시보드 "원형도면" 콤보/썸네일을 즉시 갱신한다.
        m_ctx->refreshDashboardTemplates();

        if (path.isEmpty())
            m_ctx->showStatusText(tr("기본 원형도면을 해제했습니다."));
        else
            m_ctx->showStatusText(
                tr("템플릿 '%1'을(를) 기본 원형도면으로 설정했습니다.")
                    .arg(QFileInfo(path).completeBaseName()));
        return;
    }
}

// ── SaveAsTemplateCommand ──────────────────────────────────────────────────
SaveAsTemplateCommand::SaveAsTemplateCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void SaveAsTemplateCommand::execute() {
    if (!m_ctx || !m_ctx->fileManager())
        return;

    DrawSubWindow *sub = m_ctx->activeDrawWindow();
    if (!sub || !sub->scene()) {
        QMessageBox::information(nullptr, tr("템플릿으로 저장"),
                                tr("저장할 활성 도면이 없습니다."));
        return;
    }

    // 템플릿 만들기/편집으로 연 도면만 템플릿으로 다시 저장할 수 있다.
    // (Ctrl+N 으로 연 도면은 내용이 기본 템플릿을 따르더라도 저장 불가.)
    if (!sub->property("isTemplateOrigin").toBool()) {
        QMessageBox::information(
            sub, tr("템플릿으로 저장"),
            tr("이 도면은 템플릿으로 저장할 수 없습니다.\n"
               "'템플릿 보기'에서 만든 원형도면 또는 템플릿 편집 창만 가능합니다."));
        return;
    }

    const QString dir = TemplateManager::templateDir();
    QString suggested = QDir(dir).absoluteFilePath(QStringLiteral("MyTemplate.cdt"));

    QString path = QFileDialog::getSaveFileName(
        sub, tr("템플릿으로 저장"), suggested,
        tr("CAD 템플릿 (*.cdt)"));
    if (path.isEmpty())
        return;
    if (!path.endsWith(QStringLiteral(".cdt"), Qt::CaseInsensitive))
        path += QStringLiteral(".cdt");

    if (!m_ctx->fileManager()->saveDrawing(path, sub->scene())) {
        QMessageBox::critical(sub, tr("템플릿으로 저장"),
                              tr("템플릿 저장에 실패했습니다."));
        return;
    }

    // saveDrawing() 은 최근 파일에 등록하지만, 템플릿은 "도면"이 아니므로 뺀다.
    m_ctx->fileManager()->removeRecentFile(path);

    // 방금 저장한 템플릿을 무조건 '새로만들기(Ctrl+N)'의 기본값으로 설정한다.
    TemplateManager::setDefaultTemplatePath(path);
    // 대시보드 "원형도면" 콤보/썸네일을 즉시 갱신한다 (창을 닫기 전에 먼저
    // 호출해, 닫기 확인창에서 취소해도 새 템플릿이 목록에 반영되게 한다).
    m_ctx->refreshDashboardTemplates();

    m_ctx->showStatusText(tr("템플릿으로 저장했습니다: %1").arg(path));

    // 템플릿 저장 후에는 열려 있는 모든 도면을 닫고 대시보드로 돌아간다.
    if (m_ctx->mdiArea())
        m_ctx->mdiArea()->closeAllSubWindows();
}
