#include "projectionviewtool.h"

#include "commanddock.h"
#include "extrudefeature.h"
#include "hatchitem.h"
#include "iappcontext.h"
#include "mechmodel3d.h"
#include "mechpartdata.h"
#include "mygraphicsscene.h"
#include "plateview.h"

#include <QApplication>
#include <QGraphicsView>
#include <QInputDialog>
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
    m_plate = false;
    m_revFace = false;
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
    else if (m_state == PickKind && (m_plate || m_revFace))
        ctx->commandDock()->showInteractivePrompt(
            tr(" '%1' 투상 방향 [좌측면(L)/우측면(R)/평면(T)/면 선택(F)/숨은선(H)] <L> (숨은선 %2) : ")
                .arg(m_name, m_hidden ? tr("켬") : tr("끔")),
            {tr("좌측면(L)"), tr("우측면(R)"), tr("평면(T)"), tr("면 선택(F)"),
             m_hidden ? tr("숨은선 끄기(H)") : tr("숨은선 켜기(H)")});
    else if (m_state == PickKind)
        ctx->commandDock()->showInteractivePrompt(
            tr(" '%1' 투상 방향 [좌측면(L)/우측면(R)/평면(T)/숨은선(H)] <L> (숨은선 %2) : ")
                .arg(m_name, m_hidden ? tr("켬") : tr("끔")),
            {tr("좌측면(L)"), tr("우측면(R)"), tr("평면(T)"), m_hidden ? tr("숨은선 끄기(H)") : tr("숨은선 켜기(H)")});
    else if (m_state == PickFace && !m_plate)
        ctx->commandDock()->showInteractivePrompt(tr(" 측면도에서 정면으로 볼 받침대(돌출)의 면(변)을 클릭하세요 : "), {});
    else if (m_state == PickFace)
        ctx->commandDock()->showInteractivePrompt(tr(" 정면으로 볼 판의 면(변)을 클릭하세요 : "), {});
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
    else if ((m_plate || m_revFace) && (c == QLatin1String("F") || c.contains(QLatin1String("(F)")))) {
        m_state = PickFace;
        showPrompt();
    } else
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
    if (!m_scene)
        return;
    if (m_state == PickFace) {
        // 가장 가까운 변을 미리 보여 줌
        double deg = 0.0;
        QLineF edge;
        const bool found = m_plate ? PlateView::faceAt(m_scene, m_part, pos, deg, &edge)
                                   : ProjectionView::revFaceAt(m_scene, m_part, pos, deg, &edge);
        if (found) {
            QPainterPath pp;
            pp.moveTo(edge.p1());
            pp.lineTo(edge.p2());
            updateGhostPreview(pp);
        }
        return;
    }
    if (m_state != Place)
        return;
    const double param = ProjectionView::paramFor(m_kind, m_a0, m_angle, pos);
    updateGhostPreview(ProjectionView::buildPart(m_scene, m_part, m_kind, param, m_hidden).path());
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
        if (!ProjectionView::partFrame(m_scene, id, m_a0, m_angle, m_name)) {
            QMessageBox::information(QApplication::activeWindow(), tr("투상도 만들기"),
                                     tr("이 부품은 아직 투상도를 만들 수 없습니다.\n(지원: 단면 회전체, 축, 깊은 홈 볼베어링, 단독 돌출 판)"));
            return;
        }
        m_part = id;
        m_plate = PlateView::isPlate(m_scene, id);
        // 회전체에 붙은 돌출(받침대 등, 파내기 제외)이 있으면 그 면을 골라 보조 투상도를 만들 수 있음
        m_revFace = false;
        if (!m_plate) {
            const QList<QString> ext = ExtrudeFeature::attachedTo(m_scene, id);
            for (int i = 0; i < ext.size() && !m_revFace; ++i) {
                ExtrudeInfo ei;
                m_revFace = ExtrudeFeature::resolve(m_scene, ext.at(i), ei) && !ei.cut && !ei.plateBoss &&
                            !ei.plateFront && !ei.standalone;
            }
        }
        m_state = PickKind;
        showPrompt();
        return;
    }
    if (m_state == PickFace) {
        double deg = 0.0;
        if (m_plate) {
            if (!PlateView::faceAt(m_scene, m_part, pos, deg))
                return;
            m_scene->clearOverlay();
            setKind(ProjectionView::faceKind(deg));
        } else {
            // 회전체: 측면도의 받침대 변 → 그 면 법선 φ (로컬 y, z) 방향에서 본 보조 투상도
            if (!ProjectionView::revFaceAt(m_scene, m_part, pos, deg))
                return;
            m_scene->clearOverlay();
            setKind(ProjectionView::revFaceKind(deg));
        }
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

// ── 투상도 숨기기 ──
ProjectionHideTool::ProjectionHideTool(QObject *parent) : AbstractTool(parent) {}

void ProjectionHideTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("PROJ3DHIDE (투상도 숨기기)"), CommandDock::Command);
    }
    showPrompt();
}

void ProjectionHideTool::deactivate()
{
    if (m_scene)
        m_scene->clearSelection();
    AbstractTool::deactivate();
}

void ProjectionHideTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(tr(" 숨길 투상도의 선·원을 클릭하세요 (Esc 끝) : "), {});
}

void ProjectionHideTool::cancelTool()
{
    if (m_scene)
        m_scene->clearSelection();
    emit toolFinished();
}

bool ProjectionHideTool::pickView(const QPointF &pos, QString &partUuid, QString &viewId)
{
    if (!m_scene)
        return false;
    // 가장 가까운 객체가 투상도 선이면 그것
    if (ProjectionView::viewOfItem(m_scene, itemAtPos(pos), partUuid, viewId))
        return true;
    // 구멍(HoleItem) 등 다른 객체가 먼저 잡혔으면 근처(화면 5픽셀)의 투상도 선을 찾음
    double tol = 5.0;
    if (!m_scene->views().isEmpty()) {
        const double sc = std::abs(m_scene->views().constFirst()->viewportTransform().m11());
        if (sc > 0.0001)
            tol = 5.0 / sc;
    }
    const QList<QGraphicsItem *> items =
        m_scene->items(QRectF(pos.x() - tol, pos.y() - tol, 2.0 * tol, 2.0 * tol), Qt::IntersectsItemShape);
    for (int i = 0; i < items.size(); ++i)
        if (ProjectionView::viewOfItem(m_scene, items.at(i), partUuid, viewId))
            return true;
    return false;
}

void ProjectionHideTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;
    if (button == Qt::RightButton) {
        cancelTool();
        return;
    }
    if (button != Qt::LeftButton)
        return;
    QString part, viewId;
    IAppContext *ctx = getContext();
    if (!pickView(pos, part, viewId)) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("투상도(측면도·평면도·보조 투상도) 선이 아닙니다."), CommandDock::Warning);
        return;
    }
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    if (undo)
        undo->beginMacro(tr("투상도 숨기기"));
    const bool ok = ProjectionView::setViewConcealed(ctx, m_scene, part, viewId, true);
    if (undo)
        undo->endMacro();
    if (ok && ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(
            tr("'%1' 투상도를 숨겼습니다. 측면도의 구멍은 그대로 연결됩니다 — 다시 보려면 3D 기계용 > 투상도 보이기.")
                .arg(m_scene->symbolRegistry()->symbolInfo(part).name),
            CommandDock::Info);
    showPrompt();
}

void ProjectionHideTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        cancelTool();
        return;
    }
    AbstractTool::keyPress(event);
}

// ── 투상도 보이기 ──
ProjectionShowTool::ProjectionShowTool(QObject *parent) : AbstractTool(parent) {}

void ProjectionShowTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    // 목록 창은 도구 활성화가 끝난 뒤에 띄움 (활성화 중 모달 창 금지)
    QTimer::singleShot(0, this, &ProjectionShowTool::runNow);
}

void ProjectionShowTool::runNow()
{
    IAppContext *ctx = getContext();
    if (!ctx || !m_scene) {
        emit toolFinished();
        return;
    }
    const QList<ProjectionView::ConcealedView> cvs = ProjectionView::concealedViews(m_scene);
    if (cvs.isEmpty()) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(tr("숨긴 투상도가 없습니다."), CommandDock::Info);
        emit toolFinished();
        return;
    }
    // 하나뿐이면 바로, 여러 개면 목록 ("모두" + 하나씩)
    QList<int> pick;
    if (cvs.size() == 1) {
        pick << 0;
    } else {
        QStringList labels;
        labels << tr("모두 보이기 (%1개)").arg(cvs.size());
        for (int i = 0; i < cvs.size(); ++i)
            labels << cvs.at(i).label;
        bool ok = false;
        const QString sel = QInputDialog::getItem(QApplication::activeWindow(), tr("투상도 보이기"),
                                                  tr("다시 보일 투상도:"), labels, 0, false, &ok);
        if (!ok) {
            emit toolFinished();
            return;
        }
        const int idx = labels.indexOf(sel);
        if (idx == 0)
            for (int i = 0; i < cvs.size(); ++i)
                pick << i;
        else if (idx > 0)
            pick << idx - 1;
    }
    QUndoStack *undo = ctx->undoStack();
    if (undo)
        undo->beginMacro(tr("투상도 보이기"));
    int n = 0;
    for (int i = 0; i < pick.size(); ++i)
        if (ProjectionView::setViewConcealed(ctx, m_scene, cvs.at(pick.at(i)).partUuid, cvs.at(pick.at(i)).viewId,
                                             false))
            ++n;
    if (undo)
        undo->endMacro();
    if (ctx->commandDock())
        ctx->commandDock()->logMessage(tr("투상도 %1개를 다시 보였습니다.").arg(n), CommandDock::Info);
    emit toolFinished();
}
