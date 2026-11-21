#include "mech3dviewtool.h"

#include "caditem.h"
#include "commanddock.h"
#include "depthmovetool.h"
#include "drawsubwindow.h"
#include "iappcontext.h"
#include "mechmodel3d.h"
#include "mechpartdata.h"
#include "model3dwindow.h"
#include "mygraphicsscene.h"
#include "scenesymbolregistry.h"

#include <QGraphicsItem>
#include <QKeyEvent>
#include <QTimer>

Mech3DViewTool::Mech3DViewTool(QObject *parent) : AbstractTool(parent) {}

void Mech3DViewTool::openWindow(IAppContext *ctx, const QSet<QString> &ids)
{
    if (!ctx)
        return;
    DrawSubWindow *dw = ctx->activeDrawWindow();
    if (!dw || !dw->scene())
        return;
    Model3DWindow *win = Model3DWindow::instance(ctx, nullptr);
    win->setMode(Model3DWindow::Mechanical);
    win->showDrawing(dw, true);
    win->showOnlyParts(ids);
    win->show();
    win->raise();
    win->activateWindow();
}

void Mech3DViewTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    m_picked.clear();
    m_done = false;
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("부품 3D 보기"), CommandDock::Command);
    }
    // 실행 전에 부품을 선택해 두었으면 바로 연다 (선택 → 명령)
    if (m_scene) {
        const QSet<QString> pre = MechModel3D::partIdsOfItems(m_scene, m_scene->selectedItems());
        if (!pre.isEmpty()) {
            m_picked = pre;
            QTimer::singleShot(0, this, &Mech3DViewTool::finishPick);
            return;
        }
        m_scene->clearSelection();
    }
    showPrompt();
}

void Mech3DViewTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;
    if (m_picked.isEmpty())
        ctx->commandDock()->showInteractivePrompt(
            tr(" 3D 로 볼 부품을 클릭하거나 창/교차로 고르세요 <Enter/Space = 전체 부품> : "), {});
    else
        ctx->commandDock()->showInteractivePrompt(
            tr(" 부품 %1개 선택 — 더 클릭하거나 <Enter/Space = 3D 보기> (다시 클릭 = 빼기) : ").arg(m_picked.size()), {});
}

void Mech3DViewTool::setPartHighlight(const QString &id, bool on)
{
    if (!m_scene)
        return;
    // 부품 기준 도형(외형·해칭)·돌출 선을 선택 표시로 강조
    const QList<QGraphicsItem *> items = m_scene->items();
    for (int i = 0; i < items.size(); ++i) {
        QGraphicsItem *gi = items.at(i);
        if (gi->data(MECH_DATA_PART_ID).toString() == id || gi->data(MECH_EXTRUDE_ID).toString() == id)
            gi->setSelected(on);
    }
}

void Mech3DViewTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene)
        return;
    if (button == Qt::RightButton) {
        finishPick();
        return;
    }
    if (button != Qt::LeftButton)
        return;
    IAppContext *ctx = getContext();

    // 상자 선택 두 번째 점
    if (m_boxSelecting) {
        finishBox(pos);
        return;
    }

    // 픽박스 안의 도형 → 부품, 없으면 영역 회전체·돌출 공간 안의 점
    QString id;
    QGraphicsItem *hitItem = itemAtPos(pos);
    if (hitItem) {
        const QSet<QString> ids = MechModel3D::partIdsOfItems(m_scene, QList<QGraphicsItem *>() << hitItem);
        if (!ids.isEmpty())
            id = *ids.constBegin();
    }
    if (id.isEmpty() && !hitItem) {
        bool isExtrude = false;
        id = DepthMove::partAt(m_scene, pos, isExtrude);
    }
    if (id.isEmpty()) {
        if (hitItem) {
            // 부품이 아닌 도형을 찍음
            if (ctx && ctx->commandDock())
                ctx->commandDock()->logMessage(tr("3D 부품이 아닌 도형입니다."), CommandDock::Warning);
            return;
        }
        // 빈 곳 → 상자 선택 시작 (왼→오 창, 오→왼 교차)
        m_boxSelecting = true;
        m_firstCorner = pos;
        if (ctx && ctx->commandDock())
            ctx->commandDock()->showInteractivePrompt(tr(" 반대 구석을 지정하세요 (왼→오 = 창, 오→왼 = 교차) : "), {});
        return;
    }

    const QString name = m_scene->symbolRegistry()->symbolInfo(id).name;
    if (m_picked.contains(id)) {
        m_picked.remove(id);
        setPartHighlight(id, false);
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("'%1' 빼기").arg(name), CommandDock::Info);
    } else {
        m_picked.insert(id);
        setPartHighlight(id, true);
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage(tr("'%1' 선택").arg(name), CommandDock::Info);
    }
    showPrompt();
}

void Mech3DViewTool::mouseMove(QPointF pos)
{
    if (!m_boxSelecting || !m_scene)
        return;
    const MyGraphicsScene::SelectionMode mode =
        (pos.x() > m_firstCorner.x()) ? MyGraphicsScene::Mode_Window : MyGraphicsScene::Mode_Crossing;
    m_scene->setSelectionRect(m_firstCorner, pos, mode);
}

void Mech3DViewTool::endBox()
{
    m_boxSelecting = false;
    if (m_scene)
        m_scene->clearOverlay();
}

void Mech3DViewTool::finishBox(const QPointF &p2)
{
    const bool isWindow = p2.x() > m_firstCorner.x();
    const QList<CadItem *> found = isWindow ? collectItemsInWindowRect(m_firstCorner, p2)
                                            : collectItemsInCrossingRect(m_firstCorner, p2);
    endBox();
    QList<QGraphicsItem *> items;
    for (int i = 0; i < found.size(); ++i)
        items << static_cast<QGraphicsItem *>(found.at(i));
    const QSet<QString> ids = MechModel3D::partIdsOfItems(m_scene, items);
    int added = 0;
    for (QSet<QString>::const_iterator it = ids.constBegin(); it != ids.constEnd(); ++it) {
        if (m_picked.contains(*it))
            continue;
        m_picked.insert(*it);
        setPartHighlight(*it, true);
        ++added;
    }
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(added > 0 ? tr("%1 선택: 부품 %2개 추가").arg(isWindow ? tr("창") : tr("교차")).arg(added)
                                                 : tr("상자 안에서 새 3D 부품을 찾지 못했습니다."),
                                       added > 0 ? CommandDock::Info : CommandDock::Warning);
    showPrompt();
}

void Mech3DViewTool::deactivate()
{
    endBox();
    AbstractTool::deactivate();
}

void Mech3DViewTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_boxSelecting) {      // 상자 선택 중이면 상자만 취소
            endBox();
            showPrompt();
            return;
        }
        cancelPick();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        finishPick();
        return;
    }
    AbstractTool::keyPress(event);
}

bool Mech3DViewTool::processCommand(const QString &command)
{
    // 명령줄에서 빈 입력(Enter·Space) = 끝
    if (command.trimmed().isEmpty()) {
        finishPick();
        return true;
    }
    return false;
}

void Mech3DViewTool::finishPick()
{
    if (m_done)
        return;
    m_done = true;
    endBox();
    IAppContext *ctx = getContext();
    const QSet<QString> ids = m_picked;
    if (m_scene)
        m_scene->clearSelection();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(ids.isEmpty() ? tr("전체 부품을 3D 로 봅니다.")
                                                     : tr("부품 %1개를 3D 로 봅니다.").arg(ids.size()),
                                       CommandDock::Info);
    emit toolFinished();
    openWindow(ctx, ids);
}

void Mech3DViewTool::cancelPick()
{
    if (m_done)
        return;
    m_done = true;
    if (m_scene)
        m_scene->clearSelection();
    emit toolFinished();
}
