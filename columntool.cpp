#include "columntool.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layer.h"
#include "layermanager.h"
#include "lineitem.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeyEvent>
#include <QLabel>
#include <QPainterPath>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 기하 계산 (colbox.lsp : m:pbox / boxh)
//   기준점 = 좌하단 + (dx, dy)  →  좌하단 = 기준점 - (dx, dy)
// ─────────────────────────────────────────────────────────────────────────────
ColumnGeom ColumnTool::buildGeometry(const QPointF &base, const ColumnParams &p,
                                     double dimSpacing)
{
    ColumnGeom g;
    const double w  = p.width;
    const double h  = p.height;
    const double x0 = base.x() - p.effDx();
    const double y0 = base.y() - p.effDy();
    g.rect = QRectF(x0, y0, w, h);

    // boxh : 폭 <= 깊이 이면 30도 사선(좌측 변 0.2/0.3/0.4 H), 아니면 60도 사선(하단 변 0.2/0.3/0.4 W)
    const double c30 = std::cos(qDegreesToRadians(30.0));
    const double s30 = std::sin(qDegreesToRadians(30.0));
    const double c60 = std::cos(qDegreesToRadians(60.0));
    const double s60 = std::sin(qDegreesToRadians(60.0));
    for (int bno = 2; bno <= 4; ++bno) {
        if (w <= h) {
            const double yy  = y0 + h / 10.0 * bno;
            const double len = w / c30;
            g.hatchLines.append(QLineF(QPointF(x0, yy),
                                       QPointF(x0 + len * c30, yy + len * s30)));
        } else {
            const double xx  = x0 + w / 10.0 * bno;
            const double len = h / s60;
            g.hatchLines.append(QLineF(QPointF(xx, y0),
                                       QPointF(xx + len * c60, y0 + len * s60)));
        }
    }

    // column.cdb : 기준점에 길이 100 십자
    g.crossLines.append(QLineF(base.x() - 50.0, base.y(), base.x() + 50.0, base.y()));
    g.crossLines.append(QLineF(base.x(), base.y() - 50.0, base.x(), base.y() + 50.0));

    // column.cdb : dx/dy (기준점까지, 안쪽 줄) + DX/DY (전체, 바깥 줄)
    const MyVector ll(x0, y0);
    const MyVector lr(x0 + w, y0);
    const MyVector ul(x0, y0 + h);
    const MyVector bp(base);

    if (std::fabs(p.effDx()) > 1e-6) {
        MyDimension d(ll, bp, MyVector(x0, y0 - dimSpacing));
        d.dimensionType = DimensionType::Linear;
        d.useFixedAngle = true;
        d.fixedAngle    = 0.0;
        d.hideExt1      = true;
        g.dims.append(d);
    }
    if (std::fabs(p.effDy()) > 1e-6) {
        MyDimension d(ll, bp, MyVector(x0 - dimSpacing, y0));
        d.dimensionType = DimensionType::Linear;
        d.useFixedAngle = true;
        d.fixedAngle    = 90.0;
        d.hideExt1      = true;
        g.dims.append(d);
    }
    {
        MyDimension d(ll, lr, MyVector(x0, y0 - dimSpacing * 2.0));
        d.dimensionType = DimensionType::Linear;
        d.useFixedAngle = true;
        d.fixedAngle    = 0.0;
        g.dims.append(d);
    }
    {
        MyDimension d(ll, ul, MyVector(x0 - dimSpacing * 2.0, y0));
        d.dimensionType = DimensionType::Linear;
        d.useFixedAngle = true;
        d.fixedAngle    = 90.0;
        g.dims.append(d);
    }
    return g;
}

// ─────────────────────────────────────────────────────────────────────────────
// ColumnTool
// ─────────────────────────────────────────────────────────────────────────────
ColumnTool::ColumnTool(const ColumnParams &params, QObject *parent)
    : AbstractTool(parent), m_params(params)
{}

void ColumnTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("기둥"), CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(
            tr(" 기둥 중심선 기준점을 지정하세요 <종료: 우클릭> : "), {});
    }
}

void ColumnTool::deactivate()
{
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    AbstractTool::deactivate();
}

double ColumnTool::dimSpacing() const
{
    if (!m_scene) return 250.0;
    const double s = m_scene->dimensionStyle().baselineSpacing * m_scene->drawingScale();
    return s > 1e-6 ? s : 250.0;
}

bool ColumnTool::processCommand(const QString &command)
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->parser()) return false;
    QPointF result;
    if (ctx->parser()->parseCoordinate(command.trimmed(), QPointF(0, 0), QPointF(0, 0),
                                       false, result)) {
        mousePress(result, Qt::LeftButton);
        return true;
    }
    return false;
}

void ColumnTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
        IAppContext *ctx = getContext();
        if (ctx && ctx->commandDock())
            ctx->commandDock()->setPrompt(QString::fromUtf8(" Command : "));
        emit toolFinished();
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager())
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;
    place(snapped);
}

QPainterPath ColumnTool::previewPath(const ColumnGeom &g) const
{
    QPainterPath path;
    path.addRect(g.rect);
    if (m_params.drawHatch) {
        for (int i = 0; i < g.hatchLines.size(); ++i) {
            path.moveTo(g.hatchLines.at(i).p1());
            path.lineTo(g.hatchLines.at(i).p2());
        }
    }
    if (m_params.drawCross) {
        for (int i = 0; i < g.crossLines.size(); ++i) {
            path.moveTo(g.crossLines.at(i).p1());
            path.lineTo(g.crossLines.at(i).p2());
        }
    }
    return path;
}

void ColumnTool::mouseMove(QPointF pos)
{
    if (!m_scene || m_scene->views().isEmpty()) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        auto r = ctx->snapManager()->snapPoint(pos, m_scene, QPointF());
        snapped = r.snappedPos;
        snapType = r.type;
    }
    const ColumnGeom g = buildGeometry(snapped, m_params, dimSpacing());
    updateGhostPreview(previewPath(g), QString());
    m_scene->setSnapMarker(snapped, snapType);
}

void ColumnTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        mousePress(QPointF(), Qt::RightButton);
    } else {
        AbstractTool::keyPress(event);
    }
}

void ColumnTool::ensureLayer(IAppContext *ctx, const QString &name, const QColor &color)
{
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, color, 0.25, QStringLiteral("Continuous"),
                false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

void ColumnTool::addItem(QGraphicsItem *item, const QString &layerName)
{
    IAppContext *ctx = getContext();
    if (!item || !m_scene || !ctx) return;
    if (CadItem *ci = dynamic_cast<CadItem *>(item)) ci->setLayerName(layerName);
    item->setParentItem(m_scene->getLayerRoot(layerName));
    if (item->scene() == m_scene) m_scene->removeItem(item);
    if (ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    else                  m_scene->addItem(item);
}

// colbox.lsp 의 line_b(교차점에서 break) + erase window(기둥 안쪽 선 삭제)를 합친 동작:
// 기둥 내부를 지나는 선분은 내부 구간만 잘라낸다. 기둥 외곽선과 겹치는 선은 그대로 둔다.
namespace {
struct ColumnCut {
    LineItem *item;
    QLineF    line;
    QPen      pen;
    QString   layer;
    double    t0;
    double    t1;
};
}

void ColumnTool::eraseInsideLines(const QRectF &rect)
{
    IAppContext *ctx = getContext();
    if (!m_scene || !ctx || !ctx->entityFactory()) return;

    const double eps  = 1e-6;
    const double xmin = rect.left()   + eps;
    const double xmax = rect.right()  - eps;
    const double ymin = rect.top()    + eps;
    const double ymax = rect.bottom() - eps;
    if (xmin >= xmax || ymin >= ymax) return;

    std::vector<ColumnCut> cuts;

    const QList<QGraphicsItem *> all = m_scene->items();
    for (QGraphicsItem *gi : all) {
        LineItem *li = dynamic_cast<LineItem *>(gi);
        if (!li) continue;
        const QString layerName = li->layerName();
        if (layerName == QLatin1String("C")) continue;                         // lsp : 레이어 "C" 제외
        if (gi->parentItem() != m_scene->getLayerRoot(layerName)) continue;    // 블록 내부 제외
        Layer *layer = ctx->layerManager() ? ctx->layerManager()->getLayer(layerName) : nullptr;
        if (layer && (layer->isLayerLock() || !layer->isLayerOn())) continue;

        const MyLine ml = li->getLine();
        const QPointF a = ml.start.toPointF();
        const QPointF b = ml.end.toPointF();
        const double dx = b.x() - a.x();
        const double dy = b.y() - a.y();

        // Liang-Barsky : 기둥 내부 구간 [t0, t1]
        double t0 = 0.0;
        double t1 = 1.0;
        const double p[4] = {-dx, dx, -dy, dy};
        const double q[4] = {a.x() - xmin, xmax - a.x(), a.y() - ymin, ymax - a.y()};
        bool inside = true;
        for (int i = 0; i < 4 && inside; ++i) {
            if (std::fabs(p[i]) < 1e-12) {
                if (q[i] < 0.0) inside = false;
            } else {
                const double r = q[i] / p[i];
                if (p[i] < 0.0) {
                    if (r > t1) inside = false;
                    else if (r > t0) t0 = r;
                } else {
                    if (r < t0) inside = false;
                    else if (r < t1) t1 = r;
                }
            }
        }
        if (!inside || t1 - t0 < 1e-9) continue;
        ColumnCut c;
        c.item  = li;
        c.line  = QLineF(a, b);
        c.pen   = li->pen();
        c.layer = layerName;
        c.t0    = t0;
        c.t1    = t1;
        cuts.push_back(c);
    }

    for (size_t i = 0; i < cuts.size(); ++i) {
        const ColumnCut &c = cuts[i];
        const QPointF a = c.line.p1();
        const QPointF b = c.line.p2();
        const QPointF pa(a.x() + (b.x() - a.x()) * c.t0, a.y() + (b.y() - a.y()) * c.t0);
        const QPointF pb(a.x() + (b.x() - a.x()) * c.t1, a.y() + (b.y() - a.y()) * c.t1);

        if (ctx->undoStack()) ctx->undoStack()->push(new RemoveEntityCommand(m_scene, c.item));
        else                  m_scene->removeItem(c.item);

        if (c.t0 > 1e-9)
            addItem(ctx->entityFactory()->createLine(QLineF(a, pa), c.pen), c.layer);
        if (c.t1 < 1.0 - 1e-9)
            addItem(ctx->entityFactory()->createLine(QLineF(pb, b), c.pen), c.layer);
    }
}

void ColumnTool::place(const QPointF &base)
{
    IAppContext *ctx = getContext();
    if (!m_scene || !ctx || !ctx->entityFactory()) return;

    const ColumnGeom g = buildGeometry(base, m_params, dimSpacing());
    m_scene->setGhostPath(QPainterPath(), false);

    const QString curLayer = AbstractTool::resolveLayerName(ctx, m_params.layer);
    const QPen curPen = AbstractTool::layerPenFor(ctx, curLayer);
    if (m_params.drawCross)
        ensureLayer(ctx, m_params.markLayer, QColor(255, 0, 0));
    const QString markLayer = AbstractTool::resolveLayerName(ctx, m_params.markLayer);
    const QPen markPen = AbstractTool::layerPenFor(ctx, markLayer);

    QUndoStack *stack = ctx->undoStack();
    if (stack) stack->beginMacro(tr("기둥"));

    if (m_params.eraseInside) eraseInsideLines(g.rect);

    // 외곽 : 닫힌 폴리라인 (lsp : pline p1 p2 p3 p4 c)
    std::vector<MyVector> vtx;
    vtx.push_back(MyVector(g.rect.left(),  g.rect.top()));
    vtx.push_back(MyVector(g.rect.right(), g.rect.top()));
    vtx.push_back(MyVector(g.rect.right(), g.rect.bottom()));
    vtx.push_back(MyVector(g.rect.left(),  g.rect.bottom()));
    addItem(ctx->entityFactory()->createPolyline(vtx, true, curPen), curLayer);

    if (m_params.drawHatch) {
        for (int i = 0; i < g.hatchLines.size(); ++i)
            addItem(ctx->entityFactory()->createLine(g.hatchLines.at(i), curPen), curLayer);
    }
    if (m_params.drawCross) {
        for (int i = 0; i < g.crossLines.size(); ++i)
            addItem(ctx->entityFactory()->createLine(g.crossLines.at(i), markPen), markLayer);
    }
    if (m_params.drawDims) {
        for (int i = 0; i < g.dims.size(); ++i) {
            MyDimension d = g.dims.at(i);
            applyDetailRegionScale(d);
            addItem(ctx->entityFactory()->createDimension(d, curPen), curLayer);
        }
    }

    if (stack) stack->endMacro();
}

ColumnParams ColumnTool::loadParams(MyGraphicsScene *scene)
{
    ColumnParams p;
    ToolDefaults d("ColumnTool", scene);
    p.width       = d.getDouble("Width", p.width);
    p.height      = d.getDouble("Height", p.height);
    p.centered    = d.getBool("Centered", p.centered);
    p.centerDx    = d.getDouble("CenterDx", p.centerDx);
    p.centerDy    = d.getDouble("CenterDy", p.centerDy);
    p.eraseInside = d.getBool("EraseInside", p.eraseInside);
    p.drawHatch   = d.getBool("DrawHatch", p.drawHatch);
    p.drawCross   = d.getBool("DrawCross", p.drawCross);
    p.drawDims    = d.getBool("DrawDims", p.drawDims);
    p.layer       = d.getString("Layer", p.layer);
    p.markLayer   = d.getString("MarkLayer", p.markLayer);
    return p;
}

void ColumnTool::saveParams(MyGraphicsScene *scene, const ColumnParams &p)
{
    ToolDefaults d("ColumnTool", scene);
    d.put("Width", p.width);
    d.put("Height", p.height);
    d.put("Centered", p.centered);
    d.put("CenterDx", p.centerDx);
    d.put("CenterDy", p.centerDy);
    d.put("EraseInside", p.eraseInside);
    d.put("DrawHatch", p.drawHatch);
    d.put("DrawCross", p.drawCross);
    d.put("DrawDims", p.drawDims);
    d.put("Layer", p.layer);
    d.put("MarkLayer", p.markLayer);
}

// ─────────────────────────────────────────────────────────────────────────────
// ColumnDialog
// ─────────────────────────────────────────────────────────────────────────────
ColumnDialog::ColumnDialog(const ColumnParams &params, LayerManager *layerMgr, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("기둥"));

    m_spinW  = new QDoubleSpinBox(this);
    m_spinH  = new QDoubleSpinBox(this);
    m_spinDx = new QDoubleSpinBox(this);
    m_spinDy = new QDoubleSpinBox(this);
    QDoubleSpinBox *spins[4] = {m_spinW, m_spinH, m_spinDx, m_spinDy};
    for (int i = 0; i < 4; ++i) {
        spins[i]->setDecimals(1);
        spins[i]->setRange(0.0, 1000000.0);
        spins[i]->setSingleStep(50.0);
        spins[i]->setSuffix(QStringLiteral(" mm"));
    }
    m_spinW->setRange(1.0, 1000000.0);
    m_spinH->setRange(1.0, 1000000.0);
    m_spinW->setValue(params.width);
    m_spinH->setValue(params.height);
    m_spinDx->setValue(params.centerDx);
    m_spinDy->setValue(params.centerDy);

    m_checkCentered = new QCheckBox(tr("중앙 (dx=DX/2, dy=DY/2)"), this);
    m_checkCentered->setChecked(params.centered);
    m_checkErase = new QCheckBox(tr("기둥 안쪽의 기존 선 지우기"), this);
    m_checkErase->setChecked(params.eraseInside);
    m_checkHatch = new QCheckBox(tr("기둥 표시 사선 그리기"), this);
    m_checkHatch->setChecked(params.drawHatch);
    m_checkCross = new QCheckBox(tr("기준점 십자 표시"), this);
    m_checkCross->setChecked(params.drawCross);
    m_checkDims = new QCheckBox(tr("치수선 (DX, DY, dx, dy)"), this);
    m_checkDims->setChecked(params.drawDims);

    m_comboLayer = new QComboBox(this);
    m_comboMarkLayer = new QComboBox(this);
    populateLayerCombo(m_comboLayer, layerMgr, params.layer, true);
    populateLayerCombo(m_comboMarkLayer, layerMgr, params.markLayer, false);

    m_labelInfo = new QLabel(this);
    m_labelInfo->setWordWrap(true);

    // 왼쪽 : 아이콘 / 오른쪽 : DY, DX, 중앙, dx, dy
    QLabel *iconLabel = new QLabel(this);
    iconLabel->setPixmap(QIcon(QStringLiteral(":/icons/column.png")).pixmap(130, 130));
    iconLabel->setAlignment(Qt::AlignTop | Qt::AlignHCenter);

    QFormLayout *form = new QFormLayout;
    form->addRow(tr("DY"), m_spinH);
    form->addRow(tr("DX"), m_spinW);
    form->addRow(m_checkCentered);
    form->addRow(tr("dx"), m_spinDx);
    form->addRow(tr("dy"), m_spinDy);

    QHBoxLayout *topLay = new QHBoxLayout;
    topLay->addWidget(iconLabel);
    topLay->addLayout(form, 1);

    // 체크박스 4개
    QVBoxLayout *checkLay = new QVBoxLayout;
    checkLay->addWidget(m_checkErase);
    checkLay->addWidget(m_checkHatch);
    checkLay->addWidget(m_checkCross);
    checkLay->addWidget(m_checkDims);

    // 레이어 그룹
    QGroupBox *layerGroup = new QGroupBox(tr("레이어"), this);
    QFormLayout *layerLay = new QFormLayout(layerGroup);
    layerLay->addRow(tr("기둥"), m_comboLayer);
    layerLay->addRow(tr("기준점 십자"), m_comboMarkLayer);

    QDialogButtonBox *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(topLay);
    lay->addLayout(checkLay);
    lay->addWidget(layerGroup);
    lay->addWidget(m_labelInfo);
    lay->addWidget(box);

    connect(m_spinW, &QDoubleSpinBox::valueChanged, this, &ColumnDialog::onSizeChanged);
    connect(m_spinH, &QDoubleSpinBox::valueChanged, this, &ColumnDialog::onSizeChanged);
    connect(m_checkCentered, &QCheckBox::toggled, this, &ColumnDialog::onCenteredToggled);

    onCenteredToggled(params.centered);
}

void ColumnDialog::populateLayerCombo(QComboBox *combo, LayerManager *mgr,
                                      const QString &selected, bool withCurrent)
{
    combo->clear();
    if (withCurrent)
        combo->addItem(tr("(현재 레이어)"), QString());
    if (mgr) {
        const QList<Layer> layers = mgr->getAllLayers();
        for (int i = 0; i < layers.size(); ++i) {
            QPixmap px(16, 16);
            px.fill(layers.at(i).color());
            combo->addItem(QIcon(px), layers.at(i).name(), layers.at(i).name());
        }
    }
    int idx = combo->findData(selected);
    if (idx < 0 && !selected.isEmpty()) {   // 도면에 아직 없는 레이어 (그릴 때 생성됨)
        combo->addItem(selected, selected);
        idx = combo->count() - 1;
    }
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void ColumnDialog::onCenteredToggled(bool on)
{
    m_spinDx->setEnabled(!on);
    m_spinDy->setEnabled(!on);
    if (on)
        onSizeChanged();
    else
        m_labelInfo->setText(tr("기준점은 지정한 중심선 교점이며, 기둥 좌하단 모서리에서 dx, dy 만큼 떨어진 위치입니다."));
}

void ColumnDialog::onSizeChanged()
{
    if (!m_checkCentered->isChecked()) return;
    m_spinDx->setValue(m_spinW->value() / 2.0);
    m_spinDy->setValue(m_spinH->value() / 2.0);
    m_labelInfo->setText(tr("기준점이 기둥 중앙에 놓입니다."));
}

ColumnParams ColumnDialog::params() const
{
    ColumnParams p;
    p.width       = m_spinW->value();
    p.height      = m_spinH->value();
    p.centered    = m_checkCentered->isChecked();
    p.centerDx    = m_spinDx->value();
    p.centerDy    = m_spinDy->value();
    p.eraseInside = m_checkErase->isChecked();
    p.drawHatch   = m_checkHatch->isChecked();
    p.drawCross   = m_checkCross->isChecked();
    p.drawDims    = m_checkDims->isChecked();
    p.layer       = m_comboLayer->currentData().toString();
    p.markLayer   = m_comboMarkLayer->currentData().toString();
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// ColumnCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *ColumnCommand::createCommand(IAppContext *c)
{
    return new ColumnCommand(c);
}

ColumnCommand::ColumnCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void ColumnCommand::execute()
{
    if (!m_ctx) return;
    ColumnParams params = ColumnTool::loadParams(m_ctx->activeScene());
    QWidget *parentWin = dynamic_cast<QWidget *>(m_ctx->commandDock());
    ColumnDialog dlg(params, m_ctx->layerManager(), parentWin);
    if (dlg.exec() != QDialog::Accepted) return;
    params = dlg.params();
    ColumnTool::saveParams(m_ctx->activeScene(), params);
    m_ctx->startTool(new ColumnTool(params));
}
