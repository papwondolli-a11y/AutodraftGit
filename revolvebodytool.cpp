#include "revolvebodytool.h"

#include "commanddock.h"
#include "hatchitem.h"
#include "iappcontext.h"
#include "lineitem.h"
#include "mygraphicsscene.h"
#include "revolvebodydialog.h"
#include "projectionview.h"
#include "revolveprofile.h"
#include "scenesymbolregistry.h"
#include "snapmanager.h"

#include <QApplication>
#include <QKeyEvent>
#include <QMessageBox>
#include <QPushButton>
#include <QUndoStack>

RevolveBodyTool::RevolveBodyTool(QObject *parent) : AbstractTool(parent) {}

void RevolveBodyTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    m_state = PickHatch;
    m_area = false;
    m_hatches.clear();
    m_picks.clear();
    m_region = QPainterPath();
    if (m_scene)
        m_scene->clearSelection();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("REV3D (단면 회전체 만들기)"), CommandDock::Command);
    }
    showPrompt();
}

void RevolveBodyTool::deactivate()
{
    if (m_scene) {
        m_scene->clearSelection();
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    m_hatches.clear();
    AbstractTool::deactivate();
}

void RevolveBodyTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;
    CommandDock *dock = ctx->commandDock();
    switch (m_state) {
    case PickHatch:
        dock->showInteractivePrompt(tr(" 회전할 단면의 해칭을 선택하세요 (끝: 엔터/우클릭) [영역(A)/해치(H)] <해치> : "),
                                    {tr("영역(A)"), tr("해치(H)")});
        break;
    case AreaBox1:
        dock->showInteractivePrompt(tr(" 영역: 경계가 될 객체를 고를 사각상자의 첫 점을 지정하세요 [해치(H)] : "),
                                    {tr("해치(H)")});
        break;
    case AreaBox2:
        dock->showInteractivePrompt(tr(" 사각상자의 두번째 점을 지정하세요 : "), {});
        break;
    case AreaPick:
        dock->showInteractivePrompt(
            tr(" 회전할 단면의 내부 공간을 지정하세요 (여러 곳 가능, 끝: 엔터) [복합모양(B)] : "), {tr("복합모양(B)")});
        break;
    case PickAxis1:
        dock->showInteractivePrompt(tr(" 회전축 — 중심선을 클릭하거나 첫 점을 지정하세요 : "), {});
        break;
    case PickAxis2:
        dock->showInteractivePrompt(tr(" 회전축의 둘째 점을 지정하세요 : "), {});
        break;
    }
}

HatchItem *RevolveBodyTool::hatchAt(const QPointF &pos) const
{
    if (!m_scene)
        return nullptr;
    const QList<QGraphicsItem *> items = m_scene->items(QRectF(pos.x() - 0.5, pos.y() - 0.5, 1.0, 1.0));
    for (int i = 0; i < items.size(); ++i) {
        HatchItem *h = dynamic_cast<HatchItem *>(items.at(i));
        if (h && h->isVisible() && h->sceneTransform().map(h->geometryPath()).contains(pos))
            return h;
    }
    return nullptr;
}

void RevolveBodyTool::cancelTool()
{
    if (m_scene) {
        m_scene->clearSelection();
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    m_hatches.clear();
    emit toolFinished();
}

QList<QPainterPath> RevolveBodyTool::sectionPaths() const
{
    QList<QPainterPath> paths;
    if (m_area) {
        paths << m_region;
        return paths;
    }
    for (int i = 0; i < m_hatches.size(); ++i)
        paths << m_hatches.at(i)->sceneTransform().map(m_hatches.at(i)->geometryPath());
    return paths;
}

void RevolveBodyTool::finishHatches()
{
    if (m_hatches.isEmpty()) {
        cancelTool();
        return;
    }
    m_state = PickAxis1;
    showPrompt();
}

void RevolveBodyTool::finishArea(bool complexShape)
{
    if (complexShape) {
        m_picks.clear();
        m_region = RevolveProfile::areaRegion(m_scene, m_box, m_picks);
    }
    IAppContext *ctx = getContext();
    if (m_region.isEmpty()) {
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("닫힌 공간을 찾지 못했습니다. 다시 지정하세요."), CommandDock::Warning);
        if (complexShape) {
            m_state = AreaPick;
            showPrompt();
        }
        return;
    }
    m_scene->setGhostPath(m_region, true);
    m_state = PickAxis1;
    showPrompt();
}

bool RevolveBodyTool::processCommand(const QString &command)
{
    const QString c = command.trimmed().toUpper();
    if ((m_state == PickHatch || m_state == AreaBox1) && (c == QLatin1String("A") || c.contains(QLatin1String("(A)")))) {
        m_area = true;
        m_hatches.clear();
        if (m_scene)
            m_scene->clearSelection();
        m_state = AreaBox1;
        showPrompt();
        return true;
    }
    if ((m_state == PickHatch || m_state == AreaBox1 || m_state == AreaBox2) &&
        (c == QLatin1String("H") || c.contains(QLatin1String("(H)")))) {
        m_area = false;
        m_picks.clear();
        m_region = QPainterPath();
        m_scene->setGhostPath(QPainterPath(), false);
        m_state = PickHatch;
        showPrompt();
        return true;
    }
    if (m_state == AreaPick && (c == QLatin1String("B") || c.contains(QLatin1String("(B)")))) {
        finishArea(true);
        return true;
    }
    if (c.isEmpty()) {          // 엔터
        if (m_state == PickHatch) {
            finishHatches();
            return true;
        }
        if (m_state == AreaPick) {
            if (m_picks.isEmpty())
                cancelTool();
            else
                finishArea(false);
            return true;
        }
    }
    return false;
}

void RevolveBodyTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        cancelTool();
        return;
    }
    const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space;
    if (enter && m_state == PickHatch) {
        finishHatches();
        return;
    }
    if (enter && m_state == AreaPick) {
        if (m_picks.isEmpty())
            cancelTool();
        else
            finishArea(false);
        return;
    }
    AbstractTool::keyPress(event);
}

void RevolveBodyTool::mouseMove(QPointF pos)
{
    if (!m_scene)
        return;
    // 해치 명령과 같은 방식: 스냅 표시 + 두번째 점까지 가상 사각 박스
    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        const SnapManager::SnapResult r = ctx->snapManager()->snapPoint(pos, m_scene, m_state == AreaBox2 ? m_box1 : QPointF());
        snapped = r.snappedPos;
        snapType = r.type;
    }
    if (m_state == AreaBox2) {
        QPainterPath p;
        p.addRect(QRectF(m_box1, snapped).normalized());
        updateGhostPreview(p, QString());
    }
    m_scene->setSnapMarker(snapped, snapType);
}

void RevolveBodyTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;
    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager() && (m_state == AreaBox1 || m_state == AreaBox2 || m_state == PickAxis1 ||
                                      m_state == PickAxis2))
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, m_state == AreaBox2 ? m_box1 : QPointF()).snappedPos;

    switch (m_state) {
    case PickHatch: {
        if (button == Qt::RightButton) {
            finishHatches();
            return;
        }
        if (button != Qt::LeftButton)
            return;
        HatchItem *h = hatchAt(pos);
        if (h && !m_hatches.contains(h)) {
            m_hatches.append(h);
            h->setSelected(true);
        }
        return;
    }
    case AreaBox1:
        if (button == Qt::RightButton) {
            cancelTool();
            return;
        }
        m_box1 = snapped;
        m_state = AreaBox2;
        showPrompt();
        return;
    case AreaBox2: {
        if (button == Qt::RightButton) {
            cancelTool();
            return;
        }
        m_box = QRectF(m_box1, snapped).normalized();
        m_scene->setGhostPath(QPainterPath(), false);
        if (m_box.width() < 1e-5 || m_box.height() < 1e-5) {
            m_state = AreaBox1;
            showPrompt();
            return;
        }
        m_picks.clear();
        m_region = QPainterPath();
        m_state = AreaPick;
        showPrompt();
        return;
    }
    case AreaPick: {
        if (button == Qt::RightButton) {
            if (m_picks.isEmpty())
                cancelTool();
            else
                finishArea(false);
            return;
        }
        if (button != Qt::LeftButton)
            return;
        QList<QPointF> one;
        one << pos;
        const QPainterPath p = RevolveProfile::areaRegion(m_scene, m_box, one);
        if (p.isEmpty()) {
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(tr("이 점을 둘러싼 닫힌 공간을 찾지 못했습니다."), CommandDock::Warning);
            return;
        }
        m_picks << pos;
        m_region = RevolveProfile::areaRegion(m_scene, m_box, m_picks);
        m_scene->setGhostPath(m_region, true);    // 고른 공간 표시
        return;
    }
    case PickAxis1:
        if (button == Qt::RightButton) {
            cancelTool();
            return;
        }
        if (button != Qt::LeftButton)
            return;
        // 중심선(선)을 클릭하면 그 선이 회전축
        if (LineItem *li = dynamic_cast<LineItem *>(itemAtPos(pos))) {
            const MyLine ln = li->getLine();
            finishAxis(QPointF(ln.start.x, ln.start.y), QPointF(ln.end.x, ln.end.y));
            return;
        }
        m_axis0 = snapped;
        m_state = PickAxis2;
        showPrompt();
        return;
    case PickAxis2:
        if (button == Qt::RightButton) {
            cancelTool();
            return;
        }
        if (button != Qt::LeftButton || QLineF(m_axis0, snapped).length() < 1e-6)
            return;
        finishAxis(m_axis0, snapped);
        return;
    }
}

void RevolveBodyTool::finishAxis(const QPointF &a0in, const QPointF &a1in)
{
    QPointF a0 = a0in, a1 = a1in;
    RevolveProfile::normalizeAxis(a0, a1);
    const QList<QPainterPath> paths = sectionPaths();
    QWidget *parent = QApplication::activeWindow();

    // 축 양쪽 단면이 다르면 어느 쪽을 쓸지 묻는다
    int side = RevolveProfile::SideAuto;
    bool hasUp = false, hasLo = false, symmetric = true;
    RevolveProfile::sideInfo(paths, a0, a1, hasUp, hasLo, symmetric);
    if (hasUp && hasLo && !symmetric) {
        QMessageBox box(QMessageBox::Question, tr("단면 회전체"),
                        tr("회전축 위쪽과 아래쪽 단면 모양이 다릅니다.\n어느 쪽 단면을 돌릴까요?"),
                        QMessageBox::NoButton, parent);
        QPushButton *upBtn = box.addButton(tr("위쪽 단면"), QMessageBox::AcceptRole);
        QPushButton *loBtn = box.addButton(tr("아래쪽 단면"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        if (box.clickedButton() == upBtn)
            side = RevolveProfile::SideUpper;
        else if (box.clickedButton() == loBtn)
            side = RevolveProfile::SideLower;
        else {
            cancelTool();
            return;
        }
    }
    const QList<RevolveLoop> loops = RevolveProfile::sectionLoops(paths, a0, a1, side);
    if (loops.isEmpty()) {
        QMessageBox::warning(parent, tr("단면 회전체"),
                             tr("고른 단면에서 회전체를 만들 수 없습니다.\n회전축(중심선)이 단면 옆을 지나는지 확인하세요."));
        m_state = PickAxis1;
        showPrompt();
        return;
    }
    // 같은 축·영역의 기존 회전체가 있으면 고치기 (해칭을 다시 그렸거나 영역을 다시 고른 경우)
    QList<QGraphicsItem *> items;
    for (int i = 0; i < m_hatches.size(); ++i)
        items << m_hatches.at(i);
    const QString existing =
        RevolveProfile::findExisting(m_scene, items, a0, a1, m_area ? m_region.boundingRect() : QRectF());
    QString defName = tr("회전체");
    QMap<QString, QString> initFits;
    if (!existing.isEmpty()) {
        const SymbolInfo info = m_scene->symbolRegistry()->symbolInfo(existing);
        defName = info.props.value(QStringLiteral("Name")).toString();
        initFits = RevolveProfile::fitsFromJson(info.props.value(QStringLiteral("Fits")).toString());
    }
    RevolveBodyDialog dlg(RevolveProfile::mergedByKey(RevolveProfile::cylinders(loops)), defName, initFits,
                          !existing.isEmpty(), parent);
    if (dlg.exec() != QDialog::Accepted) {
        cancelTool();
        return;
    }
    IAppContext *ctx = getContext();
    QUndoStack *undo = ctx ? ctx->undoStack() : nullptr;
    QString err;
    if (undo)
        undo->beginMacro(tr("단면 회전체"));
    const QString uuid =
        m_area ? RevolveProfile::registerRevolveArea(m_scene, undo, m_box, m_picks, m_region, a0, a1, dlg.partName(),
                                                     dlg.fits(), side, existing, &err)
               : RevolveProfile::registerRevolve(m_scene, undo, items, a0, a1, dlg.partName(), dlg.fits(), side,
                                                 existing, &err);
    if (!uuid.isEmpty())
        ProjectionView::setMirror(ctx, m_scene, uuid, dlg.mirror());
    if (undo)
        undo->endMacro();
    if (uuid.isEmpty())
        QMessageBox::warning(parent, tr("단면 회전체"), err);
    else if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(
            (existing.isEmpty() ? tr("단면 회전체 '%1' 등록") : tr("단면 회전체 '%1' 고침")).arg(dlg.partName()) +
                tr(" — 3D 기계용 > 부품 3D 보기([새로고침])로 확인하세요."),
            CommandDock::Info);
    cancelTool();
}
