#include "isodrawtool.h"

#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "isodrawing.h"
#include "layer.h"
#include "layermanager.h"
#include "mechmodel3d.h"
#include "model3dview.h"
#include "model3dwindow.h"
#include "mygraphicsscene.h"

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

void addIsoLine(IAppContext *ctx, MyGraphicsScene *scene, const QLineF &ln, const QPen &pen, const QString &layer)
{
    QGraphicsItem *item = ctx->entityFactory()->createLine(ln, pen);
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
    m_visibleOnly = new QCheckBox(tr("3D 창에 보이는 부품만"), this);
    m_hidden = new QCheckBox(tr("숨은선도 그리기"), this);
    lay->addWidget(m_visibleOnly);
    lay->addWidget(m_hidden);
    m_view->setEnabled(windowOpen);
    m_visibleOnly->setEnabled(windowOpen);
    m_visibleOnly->setChecked(windowOpen);
    (windowOpen && preferView ? m_view : m_iso)->setChecked(true);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bb);
}

bool IsoDrawDialog::viewMode() const { return m_view->isChecked(); }
bool IsoDrawDialog::visibleOnly() const { return m_visibleOnly->isEnabled() && m_visibleOnly->isChecked(); }
bool IsoDrawDialog::hidden() const { return m_hidden->isChecked(); }

// ── 계산·놓기 ──
bool IsoDrawTool::compute(IAppContext *ctx, MyGraphicsScene *scene, bool viewMode, bool visibleOnly, bool hidden,
                          ProjDrawing &out, QString *error)
{
    Q_UNUSED(ctx);
    Model3DWindow *win = openMechWindow();
    Model3D model;
    if (visibleOnly && win)
        model = win->currentModel();
    else if (scene)
        model = MechModel3D::build(scene);
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
        IsoDrawing::isoAxes(view, up, scale);
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
    if (!d.hidden.isEmpty()) {
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
    if (undo)
        undo->endMacro();
    return d.visible.size() + d.hidden.size();
}

// ── 도구 ──
IsoDrawTool::IsoDrawTool(QObject *parent, bool preferView) : AbstractTool(parent), m_preferView(preferView) {}

void IsoDrawTool::activate(MyGraphicsScene *scene)
{
    AbstractTool::activate(scene);
    m_ready = false;
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
    if (ctx && ctx->commandDock())
        ctx->commandDock()->logMessage(tr("Blender 로 모양을 확정하고 선을 계산하는 중…"), CommandDock::Info);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString err;
    const bool ok = compute(ctx, m_scene, dlg.viewMode(), dlg.visibleOnly(), dlg.hidden(), m_drawing, &err);
    QApplication::restoreOverrideCursor();
    if (!ok) {
        QMessageBox::warning(parent, tr("등각도 그리기"), err);
        QTimer::singleShot(0, this, &IsoDrawTool::finishNow);
        return;
    }
    m_ready = true;
    // 3D 보기 창이 도면을 가리지 않게 닫고 도면 창을 앞으로 (방향·부품은 이미 계산에 썼음)
    if (Model3DWindow *w = Model3DWindow::existing())
        w->close();
    if (ctx && ctx->mdiArea() && ctx->mdiArea()->window()) {
        ctx->mdiArea()->window()->raise();
        ctx->mdiArea()->window()->activateWindow();
    }
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(
            tr(" 등각도를 놓을 곳(가운데)을 지정하세요 — 실선 %1개, 숨은선 %2개 : ").arg(m_drawing.visible.size()).arg(m_drawing.hidden.size()),
            {});
}

void IsoDrawTool::deactivate()
{
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
        cancelTool();
        return;
    }
    AbstractTool::keyPress(event);
}

void IsoDrawTool::mouseMove(QPointF pos)
{
    if (!m_ready || !m_scene)
        return;
    QPainterPath p;
    for (int i = 0; i < m_drawing.visible.size(); ++i) {
        const QLineF ln = m_drawing.visible.at(i).translated(pos);
        p.moveTo(ln.p1());
        p.lineTo(ln.p2());
    }
    updateGhostPreview(p);
}

void IsoDrawTool::mousePress(QPointF pos, Qt::MouseButton button)
{
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
