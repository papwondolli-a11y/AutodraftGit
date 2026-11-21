#include "projectionviewtool.h"

#include "commanddock.h"
#include "hatchitem.h"
#include "iappcontext.h"
#include "mechmodel3d.h"
#include "mechpartdata.h"
#include "mygraphicsscene.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMessageBox>
#include <QTimer>
#include <QUndoStack>
#include <QUuid>

ProjectionViewTool::ProjectionViewTool(QObject *parent) : AbstractTool(parent) {}

void ProjectionViewTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    m_state = PickPart;
    m_part.clear();
    m_hidden = false;
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("PROJ3D (투상도 만들기)"), CommandDock::Command);
    }
    showPrompt();
}

void ProjectionViewTool::deactivate()
{
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    AbstractTool::deactivate();
}

void ProjectionViewTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;
    if (m_state == PickPart)
        ctx->commandDock()->showInteractivePrompt(
            tr(" 투상도를 만들 부품을 선택하세요 (회전체 해칭·축·베어링) : "), {});
    else if (m_state == PickKind)
        ctx->commandDock()->showInteractivePrompt(
            tr(" '%1' 투상 방향 [좌측면(L)/우측면(R)/평면(T)/숨은선(H)] <L> (숨은선 %2) : ")
                .arg(m_name, m_hidden ? tr("켬") : tr("끔")),
            {tr("좌측면(L)"), tr("우측면(R)"), tr("평면(T)"), m_hidden ? tr("숨은선 끄기(H)") : tr("숨은선 켜기(H)")});
    else
        ctx->commandDock()->showInteractivePrompt(
            tr(" %1 을(를) 놓을 위치를 지정하세요 (정면과 자동 정렬, 숨은선 %2) [숨은선(H)] : ")
                .arg(ProjectionView::kindName(m_kind), m_hidden ? tr("켬") : tr("끔")),
            {m_hidden ? tr("숨은선 끄기(H)") : tr("숨은선 켜기(H)")});
}

void ProjectionViewTool::cancelTool()
{
    if (m_scene) {
        m_scene->clearOverlay();
        m_scene->clearSelection();
    }
    emit toolFinished();
}

void ProjectionViewTool::setKind(int kind)
{
    m_kind = kind;
    m_state = Place;
    showPrompt();
}

bool ProjectionViewTool::processCommand(const QString &command)
{
    const QString c = command.trimmed().toUpper();
    if ((m_state == PickKind || m_state == Place) && (c == QLatin1String("H") || c.contains(QLatin1String("(H)")))) {
        m_hidden = !m_hidden;
        showPrompt();
        return true;
    }
    if (m_state != PickKind)
        return false;
    if (c.isEmpty() || c == QLatin1String("L") || c.contains(QLatin1String("(L)")))
        setKind(ProjectionView::Left);
    else if (c == QLatin1String("R") || c.contains(QLatin1String("(R)")))
        setKind(ProjectionView::Right);
    else if (c == QLatin1String("T") || c.contains(QLatin1String("(T)")))
        setKind(ProjectionView::Top);
    else
        return false;
    return true;
}

void ProjectionViewTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelTool();
        return;
    }
    if (m_state == PickKind && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        setKind(ProjectionView::Left);
        return;
    }
    AbstractTool::keyPress(event);
}

void ProjectionViewTool::mouseMove(QPointF pos)
{
    if (m_state != Place || !m_scene)
        return;
    const double param = ProjectionView::paramFor(m_kind, m_a0, m_angle, pos);
    updateGhostPreview(ProjectionView::build(m_loops, m_kind, m_a0, m_angle, param, m_hidden).path());
}

void ProjectionViewTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;
    if (button == Qt::RightButton) {
        cancelTool();
        return;
    }
    if (button != Qt::LeftButton)
        return;

    if (m_state == PickPart) {
        const QString id = ProjectionView::partAt(m_scene, pos);
        if (id.isEmpty())
            return;
        if (!MechModel3D::revolveSection(m_scene, id, m_loops, m_a0, m_angle, m_name)) {
            QMessageBox::information(QApplication::activeWindow(), tr("투상도 만들기"),
                                     tr("이 부품은 아직 투상도를 만들 수 없습니다.\n(지원: 단면 회전체, 축, 깊은 홈 볼베어링)"));
            return;
        }
        m_part = id;
        m_state = PickKind;
        showPrompt();
        return;
    }
    if (m_state != Place)
        return;

    IAppContext *ctx = getContext();
    const double param = ProjectionView::paramFor(m_kind, m_a0, m_angle, pos);
    const QString viewId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    if (undo)
        undo->beginMacro(tr("투상도"));
    QString err;
    const bool ok = ProjectionView::drawView(ctx, m_scene, m_part, viewId, m_kind, param, m_hidden, &err);
    if (ok)
        ProjectionView::addViewRecord(ctx, m_scene, m_part, viewId, m_kind, param, m_hidden);
    if (undo)
        undo->endMacro();
    m_scene->clearOverlay();
    if (!ok)
        QMessageBox::warning(QApplication::activeWindow(), tr("투상도 만들기"), err);
    else if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(tr("%1 — %2 생성. 모양을 고친 뒤에는 3D 기계용 > 투상도 다시 그리기.")
                                           .arg(m_name, ProjectionView::kindName(m_kind)),
                                       CommandDock::Info);
    // 같은 부품의 다른 방향을 이어서 만들 수 있게
    m_state = PickKind;
    showPrompt();
}

// ── 투상도 다시 그리기 ──
ProjectionRegenTool::ProjectionRegenTool(QObject *parent) : AbstractTool(parent) {}

void ProjectionRegenTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    IAppContext *ctx = getContext();
    if (ctx && m_scene) {
        QUndoStack *undo = ctx->undoStack();
        if (undo)
            undo->beginMacro(tr("투상도 다시 그리기"));
        const int n = ProjectionView::regenerateAll(ctx, m_scene);
        if (undo)
            undo->endMacro();
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(tr("투상도 %1개를 다시 그렸습니다.").arg(n), CommandDock::Info);
    }
    QTimer::singleShot(0, this, &ProjectionRegenTool::finishNow);
}

void ProjectionRegenTool::finishNow()
{
    emit toolFinished();
}
