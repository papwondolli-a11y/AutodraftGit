#include "isodrawtool.h"

#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "depthmovetool.h"
#include "mechpartdata.h"
#include "scenesymbolregistry.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "isodrawing.h"
#include "layer.h"
#include "layermanager.h"
#include "mechmodel3d.h"
#include "model3dview.h"
#include "model3dwindow.h"
#include "mygraphicsscene.h"
#include "myellipse.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QMdiArea>
#include <QMessageBox>
#include <QRadioButton>
#include <QTimer>
#include <QUndoStack>
#include <QVBoxLayout>
#include <cmath>

namespace {

// 숨은선 레이어 (없으면 만듦) — 투상도와 같은 이름·모양
QString isoHiddenLayer(IAppContext *ctx)
{
    const QString name = QStringLiteral("숨은선");
    if (!ctx->layerManager()->getLayer(name)) {
        Layer layer(name, QColor(255, 255, 0), 0.18, QStringLiteral("DashLine(small)"), false, true, true, false);
        ctx->layerManager()->addLayer(layer);
    }
    return name;
}

void addIsoItem(IAppContext *ctx, MyGraphicsScene *scene, QGraphicsItem *item, const QString &layer);

void addIsoLine(IAppContext *ctx, MyGraphicsScene *scene, const QLineF &ln, const QPen &pen, const QString &layer)
{
    addIsoItem(ctx, scene, ctx->entityFactory()->createLine(ln, pen), layer);
}

// 원·호 모서리: 비스듬하면 타원·타원호, 정면이면 원·호
void addIsoEllipse(IAppContext *ctx, MyGraphicsScene *scene, const ProjEllipse &e, const QPointF &at, const QPen &pen,
                   const QString &layer)
{
    const QPointF c = e.c + at;
    QGraphicsItem *item = nullptr;
    if (e.ratio > 0.9999) {
        const double r = std::hypot(e.major.x(), e.major.y());
        if (e.full)
            item = ctx->entityFactory()->createCircle(c, r, pen);
        else                                             // 호는 시작 → 끝 반시계
            item = ctx->entityFactory()->createArc(c, e.pointAt(e.t0) + at, e.pointAt(e.t1) + at, pen);
    } else {
        MyEllipse me(MyVector(c.x(), c.y()), MyVector(e.major.x(), e.major.y()), e.ratio,
                     e.full ? 0.0 : e.t0, e.full ? 6.283185307179586 : e.t1);
        item = ctx->entityFactory()->createEllipse(me, pen);
    }
    addIsoItem(ctx, scene, item, layer);
}

void addIsoItem(IAppContext *ctx, MyGraphicsScene *scene, QGraphicsItem *item, const QString &layer)
{
    if (!item)
        return;
    if (CadItem *cad = dynamic_cast<CadItem *>(item))
        cad->setLayerName(layer);
    item->setParentItem(scene->getLayerRoot(layer));
    if (item->scene() == scene)
        scene->removeItem(item);
    if (ctx->undoStack())
        ctx->undoStack()->push(new AddEntityCommand(scene, item));
    else
        scene->addItem(item);
}

// 열려 있는 기계용 3D 창 (없으면 nullptr)
Model3DWindow *openMechWindow()
{
    Model3DWindow *w = Model3DWindow::existing();
    if (w && w->mode() == Model3DWindow::Mechanical && w->isVisible())
        return w;
    return nullptr;
}

} // namespace

// ── 설정창 ──
IsoDrawDialog::IsoDrawDialog(bool windowOpen, bool preferView, QWidget *parent) : QDialog(parent)
{
    setWindowTitle(tr("등각도 그리기"));
    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("기계 부품을 도면 선으로 그립니다. Blender 로 구멍·파내기·합치기를 적용한 모양을 씁니다 (몇 초)."), this));
    QGroupBox *dir = new QGroupBox(tr("방향"), this);
    QVBoxLayout *dl = new QVBoxLayout(dir);
    m_iso = new QRadioButton(tr("KS 등각도 (세 축 120°, 축 방향 실제 길이)"), dir);
    m_view = new QRadioButton(tr("부품 3D 보기 창의 현재 화면"), dir);
    dl->addWidget(m_iso);
    dl->addWidget(m_view);
    lay->addWidget(dir);
    m_hidden = new QCheckBox(tr("숨은선도 그리기"), this);
    lay->addWidget(m_hidden);
    // 메뉴에서 시작: 확인 뒤 도면에서 부품 고르기 / 3D 창 버튼에서 시작: 창에 보이는 부품 그대로
    const bool fromWindow = windowOpen && preferView;
    QLabel *pickNote = new QLabel(fromWindow ? tr("3D 창에 보이는 부품을 그대로 그립니다.")
                                             : tr("확인을 누른 뒤 도면에서 그릴 부품을 고릅니다 (클릭 · 창/교차, Enter = 끝)."),
                                  this);
    pickNote->setWordWrap(true);
    lay->addWidget(pickNote);
    m_view->setEnabled(windowOpen);
    (windowOpen && preferView ? m_view : m_iso)->setChecked(true);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

bool IsoDrawDialog::viewMode() const { return m_view->isChecked(); }
bool IsoDrawDialog::hidden() const { return m_hidden->isChecked(); }

// ── 계산·놓기 ──
bool IsoDrawTool::compute(IAppContext *ctx, MyGraphicsScene *scene, bool viewMode, bool visibleOnly, bool hidden,
                          ProjDrawing &out, QString *error, const QSet<QString> &onlyIds)
{
    Q_UNUSED(ctx);
    Model3DWindow *win = openMechWindow();
    Model3D model;
    if (!onlyIds.isEmpty() && scene) {
        // 도면에서 고른 부품만: 나머지 부품을 숨김 목록으로 (커터·합침은 주인 부품 id 를 따라감)
        const Model3D full = MechModel3D::build(scene);
        const QList<MechModel3D::PartEntry> parts = MechModel3D::partList(full);
        QSet<QString> hiddenIds;
        for (int i = 0; i < parts.size(); ++i)
            if (!onlyIds.contains(parts.at(i).id))
                hiddenIds.insert(parts.at(i).id);
        model = MechModel3D::filtered(full, hiddenIds);
    } else if (visibleOnly && win) {
        model = win->currentModel();
    } else if (scene) {
        model = MechModel3D::build(scene);
    }
    if (model.meshes.isEmpty()) {
        if (error)
            *error = QStringLiteral("그릴 기계 부품이 없습니다.");
        return false;
    }
    QVector3D view, up;
    double scale = 1.0;
    if (viewMode && win && win->view())
        IsoDrawing::orbitAxes(win->view()->yawDeg(), win->view()->pitchDeg(), model.standUp, view, up, scale);
    else
        IsoDrawing::isoAxes(model.standUp, view, up, scale);
    QList<IsoMesh> meshes;
    if (!IsoDrawing::blenderMeshes(model, meshes, error))
        return false;
    out = IsoDrawing::build(meshes, view, up, scale, hidden);
    if (out.visible.isEmpty() && out.hidden.isEmpty()) {
        if (error)
            *error = QStringLiteral("그릴 선이 없습니다.");
        return false;
    }
    return true;
}

int IsoDrawTool::place(IAppContext *ctx, MyGraphicsScene *scene, const ProjDrawing &d, const QPointF &at)
{
    if (!ctx || !scene || !ctx->entityFactory() || !ctx->layerManager())
        return 0;
    Layer *cur = ctx->layerManager()->getCurrentLayer();
    const QString visLayer = cur ? cur->name() : QStringLiteral("0");
    const QPen visPen = AbstractTool::layerPenFor(ctx, visLayer);
    QString hidLayer;
    QPen hidPen;
    if (!d.hidden.isEmpty() || !d.hiddenEllipses.isEmpty()) {
        hidLayer = isoHiddenLayer(ctx);
        hidPen = AbstractTool::layerPenFor(ctx, hidLayer);
    }
    QUndoStack *undo = ctx->undoStack();
    if (undo)
        undo->beginMacro(QObject::tr("등각도 그리기"));
    for (int i = 0; i < d.visible.size(); ++i)
        addIsoLine(ctx, scene, d.visible.at(i).translated(at), visPen, visLayer);
    for (int i = 0; i < d.hidden.size(); ++i)
        addIsoLine(ctx, scene, d.hidden.at(i).translated(at), hidPen, hidLayer);
    for (int i = 0; i < d.visibleEllipses.size(); ++i)
        addIsoEllipse(ctx, scene, d.visibleEllipses.at(i), at, visPen, visLayer);
    for (int i = 0; i < d.hiddenEllipses.size(); ++i)
        addIsoEllipse(ctx, scene, d.hiddenEllipses.at(i), at, hidPen, hidLayer);
    if (undo)
        undo->endMacro();
    return d.visible.size() + d.hidden.size() + d.visibleEllipses.size() + d.hiddenEllipses.size();
}

// ── 도구 ──
IsoDrawTool::IsoDrawTool(QObject *parent, bool preferView) : AbstractTool(parent), m_preferView(preferView) {}

void IsoDrawTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    m_ready = false;
    m_picking = false;
    m_boxSelecting = false;
    m_picked.clear();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("ISO3D (등각도 그리기)"), CommandDock::Command);
    }
    QWidget *parent = QApplication::activeWindow();
    IsoDrawDialog dlg(openMechWindow() != nullptr, m_preferView, parent);
    if (dlg.exec() != QDialog::Accepted) {
        QTimer::singleShot(0, this, &IsoDrawTool::finishNow);
        return;
    }
    m_viewMode = dlg.viewMode();
    m_hidden = dlg.hidden();
    // 3D 창 [도면에 그리기] 에서 시작 → 부품 고르기 없이 창에 보이는 부품 그대로
    if (m_preferView && openMechWindow()) {
        m_fromWindow = true;
        QTimer::singleShot(0, this, &IsoDrawTool::finishPick);
        return;
    }
    m_fromWindow = false;
    // 실행 전에 도면에서 부품을 선택해 두었으면 그 부품으로 바로 (선택 → 명령)
    if (m_scene) {
        const QSet<QString> pre = MechModel3D::partIdsOfItems(m_scene, m_scene->selectedItems());
        if (!pre.isEmpty()) {
            m_picked = pre;
            QTimer::singleShot(0, this, &IsoDrawTool::finishPick);
            return;
        }
        m_scene->clearSelection();
    }
    // 도면 창을 앞으로 (3D 창 버튼에서 시작했어도 도면에서 고름 — 3D 창 방향은 계산 때 읽음)
    if (ctx && ctx->mdiArea() && ctx->mdiArea()->window()) {
        ctx->mdiArea()->window()->raise();
        ctx->mdiArea()->window()->activateWindow();
    }
    m_picking = true;
    showPickPrompt();
}

// ── 부품 고르기 ──
void IsoDrawTool::showPickPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock())
        return;
    if (m_picked.isEmpty())
        ctx->commandDock()->showInteractivePrompt(
            tr(" 등각도 그리기 할 부품을 선택 하세요 <도면 클릭 · 창/교차, Enter = 전체 부품> : "), {});
    else
        ctx->commandDock()->showInteractivePrompt(
            tr(" 부품 %1개 선택 — 더 고르거나 <Enter = 등각도 그리기> (다시 클릭 = 빼기) : ").arg(m_picked.size()), {});
}

void IsoDrawTool::setPartHighlight(const QString &id, bool on)
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

void IsoDrawTool::endBox()
{
    m_boxSelecting = false;
    if (m_scene)
        m_scene->clearOverlay();
}

void IsoDrawTool::finishBox(const QPointF &p2)
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
    showPickPrompt();
}

void IsoDrawTool::finishPick()
{
    if (!m_scene || m_ready)
        return;
    m_picking = false;
    endBox();
    m_scene->clearSelection();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(m_fromWindow ? tr("3D 창에 보이는 부품으로 등각도를 그립니다.")
                                       : m_picked.isEmpty() ? tr("전체 부품으로 등각도를 그립니다.")
                                                          : tr("부품 %1개로 등각도를 그립니다.").arg(m_picked.size()),
                                       CommandDock::Info);
    runCompute();
}

bool IsoDrawTool::processCommand(const QString &command)
{
    // 부품 고르는 중 명령줄 빈 입력(Enter·Space) = 고르기 끝
    if (m_picking && command.trimmed().isEmpty()) {
        finishPick();
        return true;
    }
    return false;
}

// ── 선 계산 → 놓기 단계 ──
void IsoDrawTool::runCompute()
{
    IAppContext *ctx = getContext();
    QWidget *parent = QApplication::activeWindow();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(tr("Blender 로 모양을 확정하고 선을 계산하는 중…"), CommandDock::Info);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = compute(ctx, m_scene, m_viewMode, m_fromWindow, m_hidden, m_drawing, &err,
                            m_fromWindow ? QSet<QString>() : m_picked);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(parent, tr("등각도 그리기"), err);
        QTimer::singleShot(0, this, &IsoDrawTool::finishNow);
        return;
    }
    m_ready = true;
    // 3D 보기 창이 도면을 가리지 않게 닫고 도면 창을 앞으로 (방향은 이미 계산에 썼음)
    if (Model3DWindow *w = Model3DWindow::existing())
        w->close();
    if (ctx && ctx->mdiArea() && ctx->mdiArea()->window()) {
        ctx->mdiArea()->window()->raise();
        ctx->mdiArea()->window()->activateWindow();
    }
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(
            tr(" 등각도를 놓을 곳(가운데)을 지정하세요 — 실선 %1개, 숨은선 %2개, 원·타원 %3개 : ")
                .arg(m_drawing.visible.size()).arg(m_drawing.hidden.size())
                .arg(m_drawing.visibleEllipses.size() + m_drawing.hiddenEllipses.size()),
            {});
}

void IsoDrawTool::deactivate()
{
    m_picking = false;
    endBox();
    if (m_scene)
        m_scene->clearSelection();
    if (m_scene)
        m_scene->clearOverlay();
    AbstractTool::deactivate();
}

void IsoDrawTool::finishNow()
{
    emit toolFinished();
}

void IsoDrawTool::cancelTool()
{
    if (m_scene)
        m_scene->clearOverlay();
    emit toolFinished();
}

void IsoDrawTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (m_boxSelecting) {      // 상자 선택 중이면 상자만 취소
            endBox();
            showPickPrompt();
            return;
        }
        cancelTool();
        return;
    }
    if (m_picking && (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space)) {
        finishPick();
        return;
    }
    AbstractTool::keyPress(event);
}

void IsoDrawTool::mouseMove(QPointF pos)
{
    if (m_picking && m_boxSelecting && m_scene) {   // 상자 미리보기 (왼→오 창, 오→왼 교차)
        const MyGraphicsScene::SelectionMode mode =
            (pos.x() > m_firstCorner.x()) ? MyGraphicsScene::Mode_Window : MyGraphicsScene::Mode_Crossing;
        m_scene->setSelectionRect(m_firstCorner, pos, mode);
        return;
    }
    if (!m_ready || !m_scene)
        return;
    QPainterPath p;
    for (int i = 0; i < m_drawing.visible.size(); ++i) {
        const QLineF ln = m_drawing.visible.at(i).translated(pos);
        p.moveTo(ln.p1());
        p.lineTo(ln.p2());
    }
    for (int i = 0; i < m_drawing.visibleEllipses.size(); ++i)
        p.addPath(m_drawing.visibleEllipses.at(i).path().translated(pos));
    updateGhostPreview(p);
}

void IsoDrawTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (m_picking && m_scene) {
        if (button == Qt::RightButton) {             // 고르기 끝
            finishPick();
            return;
        }
        if (button != Qt::LeftButton)
            return;
        if (m_boxSelecting) {                        // 상자 두 번째 점
            finishBox(pos);
            return;
        }
        IAppContext *ctx = getContext();
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
                if (ctx && ctx->commandDock())
                    ctx->commandDock()->logMessage(tr("3D 부품이 아닌 도형입니다."), CommandDock::Warning);
                return;
            }
            // 빈 곳 → 상자 선택 시작
            m_boxSelecting = true;
            m_firstCorner = pos;
            if (ctx && ctx->commandDock())
                ctx->commandDock()->showInteractivePrompt(tr(" 반대 구석을 지정하세요 (왼→오 = 창, 오→왼 = 교차) : "), {});
            return;
        }
        const QString name = m_scene->symbolRegistry()->symbolInfo(id).name;
        const bool remove = m_picked.contains(id);
        if (remove)
            m_picked.remove(id);
        else
            m_picked.insert(id);
        setPartHighlight(id, !remove);
        if (ctx && ctx->commandDock())
            ctx->commandDock()->logMessage((remove ? tr("'%1' 빼기") : tr("'%1' 선택")).arg(name), CommandDock::Info);
        showPickPrompt();
        return;
    }
    if (button == Qt::RightButton) {
        cancelTool();
        return;
    }
    if (button != Qt::LeftButton || !m_ready || !m_scene)
        return;
    IAppContext *ctx = getContext();
    const int n = place(ctx, m_scene, m_drawing, pos);
    m_scene->clearOverlay();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(tr("등각도 선 %1개를 그렸습니다 (모양을 고친 뒤에는 다시 그리기).").arg(n), CommandDock::Info);
    emit toolFinished();
}
