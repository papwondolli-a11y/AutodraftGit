#include "beamscheduletool.h"
#include "beamsigntool.h"
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
#include "polylineitem.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QIcon>
#include <QPainterPath>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>
#include <QUndoStack>
#include <QVBoxLayout>

#include <cmath>
#include <vector>

// ─────────────────────────────────────────────────────────────────────────────
// 공용 헬퍼
// ─────────────────────────────────────────────────────────────────────────────
namespace {
void ensureLayerImpl(IAppContext *ctx, const QString &name, const QColor &color)
{
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, color, 0.25, QStringLiteral("Continuous"),
                false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

void ensureAllLayers(IAppContext *ctx, const BeamScheduleData &data)
{
    using namespace BeamScheduleBuilder;
    ensureLayerImpl(ctx, kTableLayer,   QColor(0, 0, 255));
    ensureLayerImpl(ctx, kLabelLayer,   QColor(0, 255, 255));
    ensureLayerImpl(ctx, kValueLayer,   QColor(0, 255, 255));
    ensureLayerImpl(ctx, kPaperLayer,   QColor(255, 255, 0));
    ensureLayerImpl(ctx, data.beamLayer,    QColor(255, 255, 255));
    ensureLayerImpl(ctx, data.stirrupLayer, QColor(0, 255, 0));
    ensureLayerImpl(ctx, data.barLayer,     QColor(255, 255, 0));
}

void addItemImpl(IAppContext *ctx, MyGraphicsScene *scene, QGraphicsItem *item,
                 const QString &layerName)
{
    if (!item || !scene || !ctx) return;
    if (CadItem *ci = dynamic_cast<CadItem *>(item)) ci->setLayerName(layerName);
    item->setParentItem(scene->getLayerRoot(layerName));
    if (item->scene() == scene) scene->removeItem(item);
    if (ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(scene, item));
    else                  scene->addItem(item);
}

// 보 단면 도형 레이어 (현재 설정 + 이전에 쓰던 레이어)
QStringList shapeLayers(const BeamScheduleData &data)
{
    QStringList l;
    l << data.beamLayer << data.stirrupLayer << data.barLayer;
    l << data.replaceLayers;
    return l;
}

// 기본 표를 이루는 레이어 전체 (표 틀, 라벨, 값, 도면 사이즈, 단면 도형)
bool isScheduleLayer(const QString &name, const BeamScheduleData &data)
{
    using namespace BeamScheduleBuilder;
    return name == kTableLayer || name == kLabelLayer || name == kValueLayer
        || name == kPaperLayer || shapeLayers(data).contains(name);
}

// 사용자 표에서 이 도구가 생성하는 내용 레이어 (사용자가 그린 표 틀/라벨은 건드리지 않는다)
bool isContentLayer(const QString &name, const BeamScheduleData &data)
{
    return name == BeamScheduleBuilder::kValueLayer || shapeLayers(data).contains(name);
}

// inner 가 outer 안에 완전히 들어가는지. QRectF::contains 는 폭/높이가 0 인 사각형(수평선·수직선의
// 바운딩박스)에 항상 false 를 돌려주므로 직접 비교한다.
bool rectInside(const QRectF &inner, const QRectF &outer)
{
    return inner.left() >= outer.left() && inner.right() <= outer.right()
        && inner.top() >= outer.top() && inner.bottom() <= outer.bottom();
}

// 영역 안에 있는 도형 삭제 (onlyContent = true 이면 내용 레이어만)
void removeItemsInRect(IAppContext *ctx, MyGraphicsScene *scene, const QRectF &rect,
                       bool onlyContent, const BeamScheduleData &data)
{
    const QRectF area = rect.adjusted(-1.0, -1.0, 1.0, 1.0);
    const QList<QGraphicsItem *> all = scene->items();
    for (int i = 0; i < all.size(); ++i) {
        CadItem *ci = dynamic_cast<CadItem *>(all.at(i));
        if (!ci) continue;
        const QString layer = ci->layerName();
        if (onlyContent ? !isContentLayer(layer, data) : !isScheduleLayer(layer, data)) continue;
        if (all.at(i)->parentItem() != scene->getLayerRoot(layer)) continue;
        if (!rectInside(all.at(i)->sceneBoundingRect(), area)) continue;
        if (ctx->undoStack()) ctx->undoStack()->push(new RemoveEntityCommand(scene, all.at(i)));
        else                  scene->removeItem(all.at(i));
    }
}

// 생성 도형을 도면에 추가
void addGeomItems(IAppContext *ctx, MyGraphicsScene *scene, const ScheduleGeom &g)
{
    for (int i = 0; i < g.lines.size(); ++i) {
        const SchedLine &l = g.lines.at(i);
        addItemImpl(ctx, scene,
                    ctx->entityFactory()->createLine(l.line, AbstractTool::layerPenFor(ctx, l.layer)),
                    l.layer);
    }
    for (int i = 0; i < g.circles.size(); ++i) {
        const SchedCircle &c = g.circles.at(i);
        addItemImpl(ctx, scene,
                    ctx->entityFactory()->createCircle(c.center, c.radius,
                                                       AbstractTool::layerPenFor(ctx, c.layer)),
                    c.layer);
    }
    for (int i = 0; i < g.texts.size(); ++i) {
        const SchedText &t = g.texts.at(i);
        QFont font;
        font.setFamily(ctx->currentFontFamily());
        font.setPointSizeF(t.size);
        const QColor color = AbstractTool::layerPenFor(ctx, t.layer).color();
        addItemImpl(ctx, scene,
                    ctx->entityFactory()->createText(t.text, t.pos, font, color, 0.0, t.align),
                    t.layer);
    }
}

struct CellSegment {
    QPointF a;
    QPointF b;
};

void collectSegments(QGraphicsItem *gi, const QStringList &excludeLayers,
                     std::vector<CellSegment> &out)
{
    CadItem *ci = dynamic_cast<CadItem *>(gi);
    if (!ci) return;
    if (excludeLayers.contains(ci->layerName()))
        return;   // 이 도구가 만든 단면 도형은 칸 측정에서 제외

    if (LineItem *li = dynamic_cast<LineItem *>(gi)) {
        const MyLine ml = li->getLine();
        CellSegment s;
        s.a = gi->mapToScene(ml.start.toPointF());
        s.b = gi->mapToScene(ml.end.toPointF());
        out.push_back(s);
    } else if (PolylineItem *pi = dynamic_cast<PolylineItem *>(gi)) {
        const MyPolyline poly = pi->getPolyline();
        const size_t n = poly.vertices.size();
        for (size_t k = 0; k + 1 < n; ++k) {
            CellSegment s;
            s.a = gi->mapToScene(poly.vertices[k].toPointF());
            s.b = gi->mapToScene(poly.vertices[k + 1].toPointF());
            out.push_back(s);
        }
        if (poly.isClosed && n > 2) {
            CellSegment s;
            s.a = gi->mapToScene(poly.vertices[n - 1].toPointF());
            s.b = gi->mapToScene(poly.vertices[0].toPointF());
            out.push_back(s);
        }
    }
}
} // namespace

// ─────────────────────────────────────────────────────────────────────────────
// BeamScheduleTool
// ─────────────────────────────────────────────────────────────────────────────
BeamScheduleTool::BeamScheduleTool(const BeamScheduleData &data, QObject *parent)
    : AbstractTool(parent), m_data(data),
      m_state(data.useCustomTable ? WaitCell : WaitInsert)
{}

void BeamScheduleTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);
    m_data.table.scale = scaleValue();   // 표 정보 길이 값에 현재 도면 축척 적용

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("보 일람표"), CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(
            m_state == WaitInsert
                ? tr(" 보 일람표 삽입점(표 좌하단)을 지정하세요 <종료: 우클릭> : ")
                : tr(" 첫 번째 보의 형태란 내부를 클릭하세요 <종료: 우클릭> : "), {});
    }
}

void BeamScheduleTool::deactivate()
{
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    AbstractTool::deactivate();
}

double BeamScheduleTool::scaleValue() const
{
    if (!m_scene) return 1.0;
    const double s = m_scene->drawingScale();
    return s > 1e-9 ? s : 1.0;
}

bool BeamScheduleTool::processCommand(const QString &command)
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

void BeamScheduleTool::finishTool()
{
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(QString::fromUtf8(" Command : "));
    emit toolFinished();
}

void BeamScheduleTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        finishTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    if (m_state == WaitCell) {
        pickCell(pos);
        return;
    }

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager())
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;

    place(snapped);
    finishTool();
}

void BeamScheduleTool::mouseMove(QPointF pos)
{
    if (!m_scene || m_scene->views().isEmpty()) return;
    if (m_state != WaitInsert) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        auto r = ctx->snapManager()->snapPoint(pos, m_scene, QPointF());
        snapped = r.snappedPos;
        snapType = r.type;
    }

    // 미리보기 : 표 틀만
    const QVector<SchedLine> frame = BeamScheduleBuilder::frameLines(snapped, scaleValue());
    QPainterPath path;
    for (int i = 0; i < frame.size(); ++i) {
        path.moveTo(frame.at(i).line.p1());
        path.lineTo(frame.at(i).line.p2());
    }
    updateGhostPreview(path, BeamScheduleBuilder::kTableLayer);
    m_scene->setSnapMarker(snapped, snapType);
}

void BeamScheduleTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        finishTool();
    else
        AbstractTool::keyPress(event);
}

// 입력된 보 중 가장 큰 단면이 형태란(높이 H, 보 1개 폭의 1/3)에 들어가는 비율. 1 이면 모두 들어감
static double fitScaleFor(const QVector<BeamMark> &beams, const QRectF &cell)
{
    const BeamShapeStyle st;
    const double subW = cell.width() / 3.0;
    double fit = 1.0;
    for (int i = 0; i < beams.size(); ++i) {
        const double w = beams.at(i).width + 2.0 * st.flangeOverhang;
        const double d = beams.at(i).depth;
        if (d > 1e-9)  fit = qMin(fit, cell.height() / d);
        if (w > 1e-9)  fit = qMin(fit, subW / w);
    }
    return fit;
}

// ── [사용자 표] 형태란 클릭 → 칸 크기 측정 → 표 정보와 비교 → 생성 ──────────────
void BeamScheduleTool::pickCell(const QPointF &pos)
{
    IAppContext *ctx = getContext();
    if (!ctx) return;
    QWidget *parentWin = dynamic_cast<QWidget *>(ctx->commandDock());

    QRectF cell;
    QStringList shapeOnly;
    shapeOnly << m_data.beamLayer << m_data.stirrupLayer << m_data.barLayer << m_data.replaceLayers;
    if (!measureCell(m_scene, pos, cell, shapeOnly)) {
        QMessageBox::warning(parentWin, tr("보 일람표"),
                             tr("클릭한 위치에서 선으로 둘러싸인 칸을 찾지 못했습니다.\n"
                                "표의 형태란 안쪽을 다시 클릭하세요."));
        return;
    }

    const BeamTableInfo &t = m_data.table;
    const double expW = t.beamColW * t.scale;                   // 표 정보 x 도면 축척
    const double expH = t.rowH[BeamTableInfo::Shape] * t.scale;
    const double tolW = qMax(0.5, expW * 0.01);
    const double tolH = qMax(0.5, expH * 0.01);
    if (qAbs(cell.width() - expW) > tolW || qAbs(cell.height() - expH) > tolH) {
        QMessageBox::warning(parentWin, tr("보 일람표"),
                             tr("형태란 크기가 표 정보와 다릅니다.\n\n"
                                "측정값 : 폭 %1 x 높이 %2\n"
                                "표 정보 x 축척 1:%5 : 폭 %3 x 높이 %4\n\n"
                                "표 정보를 수정한 뒤 다시 실행하세요.")
                                 .arg(cell.width(), 0, 'f', 1).arg(cell.height(), 0, 'f', 1)
                                 .arg(expW, 0, 'f', 1).arg(expH, 0, 'f', 1)
                                 .arg(t.scale, 0, 'g', 6));
        return;
    }

    // 형태란보다 단면이 크면 사용자가 처리 방법을 선택한다
    BeamScheduleData data = m_data;
    data.shapeScale = 1.0;
    const double fit = fitScaleFor(data.packedBeams(), cell);
    if (fit < 1.0) {
        QMessageBox box(parentWin);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(tr("보 일람표"));
        box.setText(tr("보 단면이 형태란보다 큽니다.\n\n"
                       "형태란 : 폭 %1 x 높이 %2  (단면은 3개로 나뉘어 칸 폭의 1/3 사용)\n"
                       "입력된 보 중 가장 큰 단면이 칸을 넘습니다.\n\n"
                       "어떻게 처리할까요?")
                        .arg(cell.width(), 0, 'f', 1).arg(cell.height(), 0, 'f', 1));
        QPushButton *btnFit  = box.addButton(tr("칸에 맞게 축소 (%1%)").arg(qRound(fit * 95.0)), QMessageBox::AcceptRole);
        QPushButton *btnReal = box.addButton(tr("실치수로 생성"), QMessageBox::AcceptRole);
        QPushButton *btnCancel = box.addButton(tr("취소"), QMessageBox::RejectRole);
        box.setDefaultButton(btnFit);
        box.exec();
        if (box.clickedButton() == btnCancel) return;
        if (box.clickedButton() == btnFit) data.shapeScale = fit * 0.95;
        else if (box.clickedButton() != btnReal) return;
    }

    m_scene->setGhostPath(QPainterPath(), false);
    // 칸의 좌하단 (CAD 좌표는 y 가 위쪽이 + 이므로 QRectF 의 left/top 이 좌측·최소 y)
    const QPointF cellLL(cell.left(), cell.top());
    const int count = buildCustomTable(ctx, m_scene, data, cellLL);
    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("보 일람표: 보 %1개 생성").arg(count), CommandDock::Info);
        const int capacity = t.slotTotal();
        const int packed = m_data.packedBeams().size();
        if (packed > capacity)
            ctx->commandDock()->logMessage(
                tr("경고: 표 용량(%1개)을 넘는 보 %2개는 생성하지 못했습니다.").arg(capacity).arg(packed - capacity),
                CommandDock::Info);
    }
    finishTool();
}

// 클릭점을 둘러싸는 칸 : 클릭점을 지나는 수평선/수직선 중 가장 가까운 4개
bool BeamScheduleTool::measureCell(MyGraphicsScene *scene, const QPointF &pos, QRectF &cell,
                                   const QStringList &excludeLayers)
{
    if (!scene) return false;

    std::vector<CellSegment> segs;
    const QList<QGraphicsItem *> all = scene->items();
    for (int i = 0; i < all.size(); ++i) collectSegments(all.at(i), excludeLayers, segs);

    bool hasLeft = false, hasRight = false, hasBottom = false, hasTop = false;
    double left = 0.0, right = 0.0, bottom = 0.0, top = 0.0;

    for (size_t i = 0; i < segs.size(); ++i) {
        const QPointF a = segs[i].a;
        const QPointF b = segs[i].b;
        const double len = std::hypot(b.x() - a.x(), b.y() - a.y());
        if (len < 1e-9) continue;
        const double tol = 1e-6 + 1e-6 * len;

        if (std::fabs(a.y() - b.y()) <= tol) {            // 수평선
            const double x1 = qMin(a.x(), b.x());
            const double x2 = qMax(a.x(), b.x());
            if (pos.x() < x1 - tol || pos.x() > x2 + tol) continue;
            const double y = (a.y() + b.y()) / 2.0;
            if (y > pos.y() + tol) {
                if (!hasTop || y < top) { top = y; hasTop = true; }
            } else if (y < pos.y() - tol) {
                if (!hasBottom || y > bottom) { bottom = y; hasBottom = true; }
            }
        } else if (std::fabs(a.x() - b.x()) <= tol) {     // 수직선
            const double y1 = qMin(a.y(), b.y());
            const double y2 = qMax(a.y(), b.y());
            if (pos.y() < y1 - tol || pos.y() > y2 + tol) continue;
            const double x = (a.x() + b.x()) / 2.0;
            if (x > pos.x() + tol) {
                if (!hasRight || x < right) { right = x; hasRight = true; }
            } else if (x < pos.x() - tol) {
                if (!hasLeft || x > left) { left = x; hasLeft = true; }
            }
        }
    }
    if (!(hasLeft && hasRight && hasBottom && hasTop)) return false;
    cell = QRectF(QPointF(left, bottom), QPointF(right, top));
    return true;
}

int BeamScheduleTool::buildCustomTable(IAppContext *ctx, MyGraphicsScene *scene,
                                       const BeamScheduleData &data, const QPointF &cellLL)
{
    if (!scene || !ctx || !ctx->entityFactory()) return 0;

    const ScheduleGeom g = BeamScheduleBuilder::buildCustom(data, cellLL);
    ensureAllLayers(ctx, data);

    QUndoStack *stack = ctx->undoStack();
    if (stack) stack->beginMacro(QObject::tr("보 일람표"));

    // 같은 표에 다시 생성할 때 이전 내용이 겹치지 않도록 영역 안의 내용 도형 삭제
    removeItemsInRect(ctx, scene, BeamScheduleBuilder::customArea(data.table, cellLL), true, data);
    addGeomItems(ctx, scene, g);

    if (stack) stack->endMacro();

    const int packed = data.packedBeams().size();
    return packed - g.dropped;
}

// ── [기본 표] ───────────────────────────────────────────────────────────────
// 도면에 이미 있는 일람표 찾기 : B-LIST 레이어 선의 바운딩박스가 748 x 525 비율이면 표로 본다.
// (저장/불러오기 후에도 찾을 수 있도록 도형 자체로 판별)
bool BeamScheduleTool::findTable(MyGraphicsScene *scene, QRectF &rect, double &scale)
{
    if (!scene) return false;
    bool   any = false;
    double x1 = 0.0, y1 = 0.0, x2 = 0.0, y2 = 0.0;

    const QList<QGraphicsItem *> all = scene->items();
    for (int i = 0; i < all.size(); ++i) {
        LineItem *li = dynamic_cast<LineItem *>(all.at(i));
        if (!li || li->layerName() != BeamScheduleBuilder::kTableLayer) continue;
        if (li->parentItem() != scene->getLayerRoot(li->layerName())) continue;
        const MyLine ml = li->getLine();
        const double xs[2] = {ml.start.x, ml.end.x};
        const double ys[2] = {ml.start.y, ml.end.y};
        for (int k = 0; k < 2; ++k) {
            if (!any) { x1 = x2 = xs[k]; y1 = y2 = ys[k]; any = true; }
            x1 = qMin(x1, xs[k]); x2 = qMax(x2, xs[k]);
            y1 = qMin(y1, ys[k]); y2 = qMax(y2, ys[k]);
        }
    }
    if (!any) return false;

    const QPointF size = BeamScheduleBuilder::tableSize(1.0);
    const double sx = (x2 - x1) / size.x();
    const double sy = (y2 - y1) / size.y();
    if (sx < 1e-9 || qAbs(sx - sy) > sx * 0.02) return false;   // 다른 용도의 B-LIST 선

    rect = QRectF(QPointF(x1, y1), QPointF(x2, y2));
    scale = sx;
    return true;
}

void BeamScheduleTool::buildTable(IAppContext *ctx, MyGraphicsScene *scene,
                                  const BeamScheduleData &data, const QPointF &origin,
                                  double scale, bool replaceExisting)
{
    if (!scene || !ctx || !ctx->entityFactory()) return;

    const ScheduleGeom g = BeamScheduleBuilder::build(data, origin, scale);
    ensureAllLayers(ctx, data);

    QUndoStack *stack = ctx->undoStack();
    if (stack) stack->beginMacro(QObject::tr("보 일람표"));

    if (replaceExisting) {
        const QPointF size = BeamScheduleBuilder::tableSize(scale);
        removeItemsInRect(ctx, scene,
                          QRectF(origin, QPointF(origin.x() + size.x(), origin.y() + size.y())), false, data);
    }
    addGeomItems(ctx, scene, g);

    if (stack) stack->endMacro();

    if (ctx->commandDock() && g.dropped > 0)
        ctx->commandDock()->logMessage(
            QObject::tr("경고: 표 용량(%1개)을 넘는 보 %2개는 생성하지 못했습니다.")
                .arg(BeamScheduleData::kDefaultSlots).arg(g.dropped), CommandDock::Info);
}

void BeamScheduleTool::place(const QPointF &origin)
{
    IAppContext *ctx = getContext();
    if (!m_scene || !ctx) return;
    m_scene->setGhostPath(QPainterPath(), false);
    buildTable(ctx, m_scene, m_data, origin, scaleValue(), false);
    if (ctx->commandDock())
        ctx->commandDock()->logMessage(tr("보 일람표 완료"), CommandDock::Info);
}

BeamScheduleData BeamScheduleTool::loadData(MyGraphicsScene *scene)
{
    ToolDefaults d("BeamScheduleTool", scene);
    const double s = scene ? scene->drawingScale() : 1.0;
    return BeamScheduleText::fromJson(d.getString("Data", QString()), s > 1e-9 ? s : 1.0);
}

void BeamScheduleTool::saveData(MyGraphicsScene *scene, const BeamScheduleData &data)
{
    ToolDefaults d("BeamScheduleTool", scene);
    d.put("Data", BeamScheduleText::toJson(data));
}

// ─────────────────────────────────────────────────────────────────────────────
// BeamTableInfoDialog
// ─────────────────────────────────────────────────────────────────────────────
QDoubleSpinBox *BeamTableInfoDialog::makeSpin(double value)
{
    QDoubleSpinBox *s = new QDoubleSpinBox(this);
    s->setDecimals(1);
    s->setRange(0.0, 10000000.0);
    s->setSingleStep(10.0);
    s->setValue(value);
    connect(s, &QDoubleSpinBox::valueChanged, this, &BeamTableInfoDialog::updateCapacity);
    return s;
}

BeamTableInfoDialog::BeamTableInfoDialog(const BeamTableInfo &info, double drawingScale,
                                         QWidget *parent)
    : QDialog(parent), m_drawingScale(drawingScale > 1e-9 ? drawingScale : 1.0)
{
    setWindowTitle(tr("표 정보"));

    m_labelCapacity = nullptr;
    m_spinGap = nullptr;

    m_comboPaper = new QComboBox(this);
    m_comboPaper->addItems(QStringList() << "A0" << "A1" << "A2" << "A3" << "A4");
    connect(m_comboPaper, &QComboBox::currentIndexChanged, this, &BeamTableInfoDialog::updateCapacity);

    m_spinMarginW = makeSpin(0.0);
    m_spinMarginH = makeSpin(0.0);
    m_spinLabelW  = makeSpin(0.0);
    m_spinColW    = makeSpin(0.0);
    m_spinRightW  = makeSpin(0.0);
    for (int i = 0; i < BeamTableInfo::RowCount; ++i) m_spinRow[i] = makeSpin(0.0);
    m_spinGap = makeSpin(0.0);

    m_labelCapacity = new QLabel(this);
    m_labelCapacity->setWordWrap(true);

    QGroupBox *paperBox = new QGroupBox(tr("도면"), this);
    QFormLayout *paperForm = new QFormLayout(paperBox);
    paperForm->addRow(tr("도면 사이즈"), m_comboPaper);
    paperForm->addRow(tr("도면 축척 (자동 적용)"), new QLabel(QStringLiteral("1 : %1").arg(m_drawingScale, 0, 'g', 6), paperBox));
    paperForm->addRow(tr("가로 여백 합"), m_spinMarginW);
    paperForm->addRow(tr("세로 여백 합"), m_spinMarginH);

    QLabel *noteLabel = new QLabel(
        tr("입력값은 축척을 곱하기 전 크기입니다 (예: 형태란 폭 125, 높이 40).\n"
           "실제 도면에는 현재 도면 축척 1:%1 이 적용됩니다.").arg(m_drawingScale, 0, 'g', 6), this);
    noteLabel->setWordWrap(true);

    QGroupBox *colBox = new QGroupBox(tr("칸 폭"), this);
    QFormLayout *colForm = new QFormLayout(colBox);
    colForm->addRow(tr("라벨 열 폭"), m_spinLabelW);
    colForm->addRow(tr("보 열 폭 (형태란 폭)"), m_spinColW);
    colForm->addRow(tr("오른쪽 칸 폭"), m_spinRightW);

    static const char *kRowNames[BeamTableInfo::RowCount] = {
        "스트럽", "하부근", "상부근", "크기", "위치", "형태 (형태란 높이)", "부호"};
    QGroupBox *rowBox = new QGroupBox(tr("칸 높이 (아래 → 위)"), this);
    QFormLayout *rowForm = new QFormLayout(rowBox);
    for (int i = 0; i < BeamTableInfo::RowCount; ++i)
        rowForm->addRow(QString::fromUtf8(kRowNames[i]), m_spinRow[i]);
    rowForm->addRow(tr("띠 사이 간격"), m_spinGap);

    QPushButton *btnReset = new QPushButton(tr("기본 양식 × 축척으로 되돌리기"), this);
    connect(btnReset, &QPushButton::clicked, this, &BeamTableInfoDialog::onResetDefaults);

    QHBoxLayout *midLay = new QHBoxLayout;
    QVBoxLayout *leftLay = new QVBoxLayout;
    leftLay->addWidget(paperBox);
    leftLay->addWidget(colBox);
    leftLay->addStretch(1);
    midLay->addLayout(leftLay);
    midLay->addWidget(rowBox);

    QDialogButtonBox *box = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(noteLabel);
    lay->addLayout(midLay);
    lay->addWidget(m_labelCapacity);
    lay->addWidget(btnReset);
    lay->addWidget(box);

    setInfo(info);
}

void BeamTableInfoDialog::setInfo(const BeamTableInfo &t)
{
    const int idx = m_comboPaper->findText(t.paperSize.trimmed().toUpper());
    m_comboPaper->setCurrentIndex(idx >= 0 ? idx : 1);
    m_spinMarginW->setValue(t.marginW);
    m_spinMarginH->setValue(t.marginH);
    m_spinLabelW->setValue(t.labelColW);
    m_spinColW->setValue(t.beamColW);
    m_spinRightW->setValue(t.rightColW);
    for (int i = 0; i < BeamTableInfo::RowCount; ++i) m_spinRow[i]->setValue(t.rowH[i]);
    m_spinGap->setValue(t.bandGap);
    updateCapacity();
}

BeamTableInfo BeamTableInfoDialog::info() const
{
    BeamTableInfo t;
    t.paperSize = m_comboPaper->currentText();
    t.scale     = m_drawingScale;
    t.marginW   = m_spinMarginW->value();
    t.marginH   = m_spinMarginH->value();
    t.labelColW = m_spinLabelW->value();
    t.beamColW  = m_spinColW->value();
    t.rightColW = m_spinRightW->value();
    for (int i = 0; i < BeamTableInfo::RowCount; ++i) t.rowH[i] = m_spinRow[i]->value();
    t.bandGap   = m_spinGap->value();
    return t;
}

void BeamTableInfoDialog::updateCapacity()
{
    if (!m_labelCapacity || !m_spinGap) return;   // 생성 중 시그널 방지
    const BeamTableInfo t = info();
    double pw = 0.0, ph = 0.0;
    BeamTableInfo::paperMm(t.paperSize, pw, ph);
    m_labelCapacity->setText(
        tr("도면 %1 (%2 x %3 mm)\n"
           "들어갈 수 있는 보 : %4단 x %5열 = %6개  (띠 높이 %7)")
            .arg(t.paperSize).arg(pw, 0, 'f', 0).arg(ph, 0, 'f', 0)
            .arg(t.bands()).arg(t.columns()).arg(t.slotTotal()).arg(t.bandPitch(), 0, 'f', 1));
}

void BeamTableInfoDialog::onResetDefaults()
{
    BeamTableInfo t = BeamTableInfo::defaults(m_drawingScale);
    t.paperSize = m_comboPaper->currentText();
    setInfo(t);
}

// ─────────────────────────────────────────────────────────────────────────────
// BeamScheduleDialog
// ─────────────────────────────────────────────────────────────────────────────
BeamScheduleDialog::BeamScheduleDialog(const BeamScheduleData &data, double drawingScale,
                                       MyGraphicsScene *scene, LayerManager *layerMgr,
                                       QWidget *parent)
    : QDialog(parent), m_data(data), m_drawingScale(drawingScale), m_scene(scene)
{
    setWindowTitle(tr("보 일람표"));
    resize(900, 600);
    m_data.normalizeSlots();

    // 표 방식
    m_radioDefault = new QRadioButton(tr("기본 표 삽입"), this);
    m_radioCustom  = new QRadioButton(tr("사용자표 사용"), this);
    m_radioDefault->setChecked(!m_data.useCustomTable);
    m_radioCustom->setChecked(m_data.useCustomTable);
    m_btnTableInfo = new QPushButton(tr("표 정보 추가"), this);
    m_labelModeInfo = new QLabel(this);

    QHBoxLayout *modeLay = new QHBoxLayout;
    modeLay->addWidget(m_radioDefault);
    modeLay->addWidget(m_radioCustom);
    modeLay->addWidget(m_btnTableInfo);
    modeLay->addStretch(1);

    // 보 목록 (부호만 표시)
    m_listSlots = new QListWidget(this);
    m_listSlots->setMinimumWidth(150);
    m_listSlots->setContextMenuPolicy(Qt::CustomContextMenu);

    // 보 기본 정보
    m_editName = new QLineEdit(this);
    m_spinWidth = new QDoubleSpinBox(this);
    m_spinDepth = new QDoubleSpinBox(this);
    QDoubleSpinBox *dims[2] = {m_spinWidth, m_spinDepth};
    for (int i = 0; i < 2; ++i) {
        dims[i]->setDecimals(0);
        dims[i]->setRange(100.0, 5000.0);
        dims[i]->setSingleStep(50.0);
    }

    QFormLayout *basicForm = new QFormLayout;
    basicForm->addRow(tr("부호"), m_editName);
    basicForm->addRow(tr("폭 b"), m_spinWidth);
    basicForm->addRow(tr("춤 D (슬래브 포함)"), m_spinDepth);

    // 단면 3개 : 상부근 / 하부근 / 스트럽 그룹
    QHBoxLayout *secLay = new QHBoxLayout;
    for (int j = 0; j < 3; ++j) {
        SectionWidgets &w = m_sec[j];
        QGroupBox *box = new QGroupBox(BeamScheduleText::position(j), this);
        QVBoxLayout *boxLay = new QVBoxLayout(box);
        QGroupBox *topGroup = new QGroupBox(tr("상부근"), box);
        QGroupBox *botGroup = new QGroupBox(tr("하부근"), box);
        QGroupBox *stirGroup = new QGroupBox(tr("스트럽"), box);
        QFormLayout *topForm = new QFormLayout(topGroup);
        QFormLayout *botForm = new QFormLayout(botGroup);
        QFormLayout *stirForm = new QFormLayout(stirGroup);

        w.stirrupDia = new QComboBox(box);
        fillDiaCombo(w.stirrupDia);
        w.pitch = new QDoubleSpinBox(box);
        w.pitch->setDecimals(0);
        w.pitch->setRange(10.0, 1000.0);
        w.pitch->setSingleStep(25.0);
        w.high = new QCheckBox(tr("고강도 (H)"), box);

        w.botDia = new QComboBox(box);
        fillDiaCombo(w.botDia);
        w.bot1 = new QSpinBox(box);
        w.bot2 = new QSpinBox(box);

        w.topDia = new QComboBox(box);
        fillDiaCombo(w.topDia);
        w.top1 = new QSpinBox(box);
        w.top2 = new QSpinBox(box);

        QSpinBox *counts[4] = {w.bot1, w.bot2, w.top1, w.top2};
        for (int k = 0; k < 4; ++k) counts[k]->setRange(0, 20);

        topForm->addRow(tr("직경"), w.topDia);
        topForm->addRow(tr("1열(상)"), w.top1);
        topForm->addRow(tr("2열(하)"), w.top2);
        botForm->addRow(tr("직경"), w.botDia);
        botForm->addRow(tr("1열(상)"), w.bot1);
        botForm->addRow(tr("2열(하)"), w.bot2);
        stirForm->addRow(tr("직경"), w.stirrupDia);
        stirForm->addRow(tr("간격"), w.pitch);
        stirForm->addRow(w.high);
        boxLay->addWidget(topGroup);
        boxLay->addWidget(botGroup);
        boxLay->addWidget(stirGroup);
        secLay->addWidget(box);
    }

    QPushButton *btnCopyCenter = new QPushButton(tr("중앙 → 양 단부 복사"), this);
    QPushButton *btnCopyIntExt = new QPushButton(tr("INT → EXT 복사"), this);
    QPushButton *btnCopyPrev   = new QPushButton(tr("앞 보 복사"), this);
    QHBoxLayout *btnLay = new QHBoxLayout;
    btnLay->addWidget(btnCopyCenter);
    btnLay->addWidget(btnCopyIntExt);
    btnLay->addWidget(btnCopyPrev);
    btnLay->addStretch(1);

    m_editorPanel = new QWidget(this);
    QVBoxLayout *rightLay = new QVBoxLayout(m_editorPanel);
    rightLay->setContentsMargins(0, 0, 0, 0);
    rightLay->addLayout(basicForm);
    rightLay->addLayout(secLay);
    rightLay->addLayout(btnLay);
    rightLay->addStretch(1);

    // 목록 아래 : 구조도에서 부호 가져오기 / 리스트 초기화
    QPushButton *btnImport = new QPushButton(tr("구조도에서 부호 가져오기"), this);
    QPushButton *btnReset  = new QPushButton(tr("리스트 초기화"), this);
    QHBoxLayout *listBtnLay = new QHBoxLayout;
    listBtnLay->addWidget(btnImport);
    listBtnLay->addWidget(btnReset);
    QVBoxLayout *listLay = new QVBoxLayout;
    listLay->addWidget(m_listSlots, 1);
    listLay->addLayout(listBtnLay);

    QHBoxLayout *bodyLay = new QHBoxLayout;
    bodyLay->addLayout(listLay);
    bodyLay->addWidget(m_editorPanel, 1);

    // 레이어 그룹 : 보 단면 도형에 쓰는 3개 레이어
    m_comboBeamLayer    = new QComboBox(this);
    m_comboStirrupLayer = new QComboBox(this);
    m_comboBarLayer     = new QComboBox(this);
    populateLayerCombo(m_comboBeamLayer,    layerMgr, m_data.beamLayer);
    populateLayerCombo(m_comboStirrupLayer, layerMgr, m_data.stirrupLayer);
    populateLayerCombo(m_comboBarLayer,     layerMgr, m_data.barLayer);

    QGroupBox *layerGroup = new QGroupBox(tr("레이어"), this);
    QHBoxLayout *layerLay = new QHBoxLayout(layerGroup);
    layerLay->addWidget(new QLabel(tr("보 외곽"), layerGroup));
    layerLay->addWidget(m_comboBeamLayer, 1);
    layerLay->addWidget(new QLabel(tr("스트럽"), layerGroup));
    layerLay->addWidget(m_comboStirrupLayer, 1);
    layerLay->addWidget(new QLabel(tr("철근"), layerGroup));
    layerLay->addWidget(m_comboBarLayer, 1);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addLayout(modeLay);
    lay->addWidget(m_labelModeInfo);
    lay->addLayout(bodyLay, 1);
    lay->addWidget(layerGroup);
    lay->addWidget(buttons);

    connect(m_listSlots, &QListWidget::currentRowChanged, this, &BeamScheduleDialog::onSlotChanged);
    connect(m_listSlots, &QListWidget::customContextMenuRequested, this, &BeamScheduleDialog::onSlotContextMenu);
    connect(m_editName, &QLineEdit::textEdited, this, &BeamScheduleDialog::onNameEdited);
    connect(m_editName, &QLineEdit::editingFinished, this, &BeamScheduleDialog::onNameEditingFinished);
    connect(m_radioDefault, &QRadioButton::toggled, this, &BeamScheduleDialog::onModeChanged);
    connect(m_btnTableInfo, &QPushButton::clicked, this, &BeamScheduleDialog::onTableInfoClicked);
    connect(btnImport, &QPushButton::clicked, this, &BeamScheduleDialog::onImportSigns);
    connect(btnReset, &QPushButton::clicked, this, &BeamScheduleDialog::onResetList);
    connect(btnCopyCenter, &QPushButton::clicked, this, &BeamScheduleDialog::onCopyCenterToEnds);
    connect(btnCopyIntExt, &QPushButton::clicked, this, &BeamScheduleDialog::onCopyIntToExt);
    connect(btnCopyPrev, &QPushButton::clicked, this, &BeamScheduleDialog::onCopyFromPrevious);

    rebuildSlots();
}

// 목록에는 부호만 표시
QString BeamScheduleDialog::slotLabel(int slot) const
{
    const QString name = m_data.beams.at(slot).name.trimmed();
    return name.isEmpty() ? tr("(비어 있음)") : name;
}

void BeamScheduleDialog::refreshSlotText(int slot)
{
    QListWidgetItem *item = m_listSlots->item(slot);
    if (item && slot < m_data.beams.size()) item->setText(slotLabel(slot));
}

void BeamScheduleDialog::updateModeInfo()
{
    const BeamTableInfo &t = m_data.table;
    if (m_data.useCustomTable) {
        m_labelModeInfo->setText(
            tr("사용자표 : 도면 %1 기준 %2단 x %3열 = 보 %4개까지 생성 (표 정보 버튼에서 수정)")
                .arg(t.paperSize).arg(t.bands()).arg(t.columns()).arg(t.slotTotal()));
    } else {
        m_labelModeInfo->setText(
            tr("기본 표 : 도면 %1 / %2단 x %3열 = 보 %4개 (blocks/etc/beamlist.cdb 양식)")
                .arg(t.paperSize).arg(BeamScheduleData::kDefaultBands)
                .arg(BeamScheduleData::kDefaultColumns).arg(BeamScheduleData::kDefaultSlots));
    }
}

// 슬롯 수를 모드(기본 25개 / 표 정보 용량)에 맞추고 목록을 다시 만든다
void BeamScheduleDialog::rebuildSlots()
{
    commitCurrent();
    const int keep = m_currentSlot;
    m_currentSlot = -1;

    m_data.normalizeSlots();
    updateModeInfo();

    m_listSlots->blockSignals(true);
    m_listSlots->clear();
    for (int i = 0; i < m_data.beams.size(); ++i) m_listSlots->addItem(slotLabel(i));
    m_listSlots->blockSignals(false);

    m_editorPanel->setEnabled(!m_data.beams.isEmpty());
    if (m_data.beams.isEmpty()) return;

    int row = keep < 0 ? 0 : keep;
    if (row >= m_data.beams.size()) row = m_data.beams.size() - 1;
    m_listSlots->blockSignals(true);
    m_listSlots->setCurrentRow(row);
    m_listSlots->blockSignals(false);
    loadSlot(row);
}

void BeamScheduleDialog::fillDiaCombo(QComboBox *combo) const
{
    static const int kDias[] = {10, 13, 16, 19, 22, 25, 29, 32, 35, 38, 41};
    for (size_t i = 0; i < sizeof(kDias) / sizeof(kDias[0]); ++i)
        combo->addItem(QStringLiteral("D%1").arg(kDias[i]), kDias[i]);
}

void BeamScheduleDialog::setSectionWidgets(int index, const BeamSection &s)
{
    SectionWidgets &w = m_sec[index];
    int i = w.stirrupDia->findData(s.stirrupDia);
    w.stirrupDia->setCurrentIndex(i >= 0 ? i : 0);
    w.pitch->setValue(s.stirrupPitch);
    w.high->setChecked(s.stirrupHigh);
    i = w.botDia->findData(s.bottom.dia);
    w.botDia->setCurrentIndex(i >= 0 ? i : 0);
    w.bot1->setValue(s.bottom.row1);
    w.bot2->setValue(s.bottom.row2);
    i = w.topDia->findData(s.top.dia);
    w.topDia->setCurrentIndex(i >= 0 ? i : 0);
    w.top1->setValue(s.top.row1);
    w.top2->setValue(s.top.row2);
}

BeamSection BeamScheduleDialog::readSectionWidgets(int index) const
{
    const SectionWidgets &w = m_sec[index];
    BeamSection s;
    s.stirrupDia   = w.stirrupDia->currentData().toInt();
    s.stirrupPitch = w.pitch->value();
    s.stirrupHigh  = w.high->isChecked();
    s.bottom.dia   = w.botDia->currentData().toInt();
    s.bottom.row1  = w.bot1->value();
    s.bottom.row2  = w.bot2->value();
    s.top.dia      = w.topDia->currentData().toInt();
    s.top.row1     = w.top1->value();
    s.top.row2     = w.top2->value();
    return s;
}

void BeamScheduleDialog::commitCurrent()
{
    if (m_currentSlot < 0 || m_loading || m_currentSlot >= m_data.beams.size()) return;
    BeamMark &m = m_data.beams[m_currentSlot];
    m.name  = m_editName->text().trimmed();
    m.width = m_spinWidth->value();
    m.depth = m_spinDepth->value();
    for (int j = 0; j < 3; ++j) m.sec[j] = readSectionWidgets(j);
    refreshSlotText(m_currentSlot);
}

void BeamScheduleDialog::loadSlot(int slot)
{
    if (slot < 0 || slot >= m_data.beams.size()) return;
    m_loading = true;
    const BeamMark &m = m_data.beams.at(slot);
    m_editName->setText(m.name);
    m_spinWidth->setValue(m.width);
    m_spinDepth->setValue(m.depth);
    for (int j = 0; j < 3; ++j) setSectionWidgets(j, m.sec[j]);
    m_loading = false;
    m_currentSlot = slot;
}

void BeamScheduleDialog::onSlotChanged(int row)
{
    if (row < 0) return;
    commitCurrent();
    loadSlot(row);
}

void BeamScheduleDialog::onNameEdited(const QString &text)
{
    Q_UNUSED(text);
    commitCurrent();
}

// 같은 부호를 가진 슬롯이 둘 이상이면 그 부호 목록 (대소문자 무시)
QStringList BeamScheduleDialog::duplicateNames() const
{
    QStringList dup;
    QStringList seen;
    for (int i = 0; i < m_data.beams.size(); ++i) {
        const QString n = m_data.beams.at(i).name.trimmed();
        if (n.isEmpty()) continue;
        if (seen.contains(n, Qt::CaseInsensitive)) {
            if (!dup.contains(n, Qt::CaseInsensitive)) dup << n;
        } else {
            seen << n;
        }
    }
    return dup;
}

void BeamScheduleDialog::onNameEditingFinished()
{
    if (m_currentSlot < 0 || m_loading) return;
    commitCurrent();
    const QString name = m_data.beams.at(m_currentSlot).name;
    if (name.isEmpty()) return;
    for (int i = 0; i < m_data.beams.size(); ++i) {
        if (i != m_currentSlot && m_data.beams.at(i).name.trimmed().compare(name, Qt::CaseInsensitive) == 0) {
            QMessageBox::warning(this, tr("보 일람표"),
                                 tr("부호 \"%1\" 이(가) 이미 목록에 있습니다.\n"
                                    "같은 부호가 중복되지 않게 수정하세요.").arg(name));
            return;
        }
    }
}

void BeamScheduleDialog::accept()
{
    commitCurrent();
    const QStringList dup = duplicateNames();
    if (!dup.isEmpty()) {
        const QMessageBox::StandardButton r = QMessageBox::warning(
            this, tr("보 일람표"),
            tr("중복된 부호가 있습니다 : %1\n\n그대로 진행하시겠습니까?").arg(dup.join(QStringLiteral(", "))),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (r != QMessageBox::Yes) return;
    }
    QDialog::accept();
}

void BeamScheduleDialog::populateLayerCombo(QComboBox *combo, LayerManager *mgr, const QString &selected)
{
    combo->clear();
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

// 부호가 있는 보를 앞쪽으로 모으고 빈 슬롯은 뒤로 보낸다 (순서는 유지)
void BeamScheduleDialog::compactSlots()
{
    QVector<BeamMark> packed;
    for (int i = 0; i < m_data.beams.size(); ++i) {
        if (!m_data.beams.at(i).isEmpty()) packed.append(m_data.beams.at(i));
    }
    const int total = m_data.beams.size();
    while (packed.size() < total) packed.append(BeamMark());
    m_data.beams = packed;
}

// 목록 우클릭 : 해당 보의 데이터 삭제 (뒤의 보가 앞으로 당겨진다)
void BeamScheduleDialog::onSlotContextMenu(const QPoint &pos)
{
    QListWidgetItem *item = m_listSlots->itemAt(pos);
    if (!item) return;
    const int row = m_listSlots->row(item);
    if (row < 0 || row >= m_data.beams.size()) return;

    QMenu menu(this);
    QAction *actDelete = menu.addAction(tr("삭제"));
    if (m_data.beams.at(row).isEmpty())
        actDelete->setEnabled(false);
    if (menu.exec(m_listSlots->viewport()->mapToGlobal(pos)) != actDelete) return;

    // 삭제 후 뒤의 보를 앞으로 당겨 빈 슬롯이 중간에 생기지 않게 한다
    commitCurrent();
    m_data.beams.remove(row);
    m_data.beams.append(BeamMark());
    m_currentSlot = -1;
    rebuildSlots();
    m_listSlots->setCurrentRow(qMin(row, m_listSlots->count() - 1));
}

// 리스트 초기화 : 모든 슬롯을 "(비어 있음)" 으로 (슬롯 수는 모드에 맞게 유지)
void BeamScheduleDialog::onResetList()
{
    commitCurrent();
    int filled = 0;
    for (int i = 0; i < m_data.beams.size(); ++i)
        if (!m_data.beams.at(i).isEmpty()) ++filled;
    if (filled == 0) return;

    if (QMessageBox::question(this, tr("리스트 초기화"),
                              tr("입력된 보 %1개의 데이터가 모두 삭제되고 모든 슬롯이 비어 있음으로 바뀝니다.\n"
                                 "초기화할까요? (확인 버튼을 누르기 전에는 도면에 반영되지 않습니다)").arg(filled),
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes)
        return;

    m_data.beams = QVector<BeamMark>(m_data.slotCount());   // 모든 슬롯 기본값(부호 없음)
    m_currentSlot = -1;
    rebuildSlots();
}

// 구조도(.cdb)에서 보 부호를 읽어 목록에 없는 부호만 빈 줄로 추가 (기존 보 데이터는 건드리지 않음)
void BeamScheduleDialog::onImportSigns()
{
    commitCurrent();
    QStringList existing;
    for (int i = 0; i < m_data.beams.size(); ++i) {
        const QString n = m_data.beams.at(i).name.trimmed();
        if (!n.isEmpty()) existing << n;
    }

    BeamSignImportDialog dlg(existing, m_scene, this);
    if (dlg.exec() != QDialog::Accepted) return;

    const QStringList names = dlg.selectedNames();
    int added = 0;
    int skipped = 0;
    for (int k = 0; k < names.size(); ++k) {
        if (existing.contains(names.at(k), Qt::CaseInsensitive)) { ++skipped; continue; }
        int slot = -1;
        for (int i = 0; i < m_data.beams.size(); ++i) {
            if (m_data.beams.at(i).isEmpty()) { slot = i; break; }
        }
        if (slot < 0) {
            m_data.beams.append(BeamMark());
            slot = m_data.beams.size() - 1;
        }
        m_data.beams[slot].name = names.at(k);
        existing << names.at(k);
        ++added;
    }

    rebuildSlots();
    if (added > 0 && m_data.beams.size() > m_data.slotCount())
        QMessageBox::information(this, tr("보 일람표"),
                                 tr("표 용량(%1개)보다 많은 보가 입력되었습니다. 용량을 넘는 보는 표에 생성되지 않습니다.")
                                     .arg(m_data.slotCount()));
    QMessageBox::information(this, tr("보 부호 가져오기"),
                             tr("%1개를 추가했습니다 (이미 있어 건너뜀 %2개).").arg(added).arg(skipped));
}

void BeamScheduleDialog::onCopyIntToExt()
{
    setSectionWidgets(2, readSectionWidgets(0));
}

void BeamScheduleDialog::onModeChanged()
{
    commitCurrent();
    m_data.useCustomTable = m_radioCustom->isChecked();
    rebuildSlots();
}

void BeamScheduleDialog::onTableInfoClicked()
{
    BeamTableInfoDialog dlg(m_data.table, m_drawingScale, this);
    if (dlg.exec() != QDialog::Accepted) return;
    m_data.table = dlg.info();
    rebuildSlots();
}

void BeamScheduleDialog::onCopyCenterToEnds()
{
    const BeamSection center = readSectionWidgets(1);
    setSectionWidgets(0, center);
    setSectionWidgets(2, center);
}

void BeamScheduleDialog::onCopyFromPrevious()
{
    if (m_currentSlot <= 0) return;
    commitCurrent();
    const QString keepName = m_data.beams.at(m_currentSlot).name;
    m_data.beams[m_currentSlot] = m_data.beams.at(m_currentSlot - 1);
    m_data.beams[m_currentSlot].name = keepName;
    loadSlot(m_currentSlot);
    refreshSlotText(m_currentSlot);
}

BeamScheduleData BeamScheduleDialog::data()
{
    commitCurrent();
    compactSlots();
    m_data.useCustomTable = m_radioCustom->isChecked();
    m_data.beamLayer    = m_comboBeamLayer->currentData().toString();
    m_data.stirrupLayer = m_comboStirrupLayer->currentData().toString();
    m_data.barLayer     = m_comboBarLayer->currentData().toString();
    return m_data;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeamScheduleCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *BeamScheduleCommand::createCommand(IAppContext *c)
{
    return new BeamScheduleCommand(c);
}

BeamScheduleCommand::BeamScheduleCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void BeamScheduleCommand::execute()
{
    if (!m_ctx) return;
    MyGraphicsScene *scene = m_ctx->activeScene();
    const double scale = (scene && scene->drawingScale() > 1e-9) ? scene->drawingScale() : 1.0;

    BeamScheduleData data = BeamScheduleTool::loadData(scene);
    const QStringList oldLayers = QStringList() << data.beamLayer << data.stirrupLayer << data.barLayer;
    QWidget *parentWin = dynamic_cast<QWidget *>(m_ctx->commandDock());
    BeamScheduleDialog dlg(data, scale, scene, m_ctx->layerManager(), parentWin);
    if (dlg.exec() != QDialog::Accepted) return;
    data = dlg.data();
    data.replaceLayers = oldLayers;   // 레이어를 바꿨어도 이전 레이어의 기존 도형을 함께 갱신
    BeamScheduleTool::saveData(scene, data);

    // 기본 표 : 이미 일람표가 있으면 새로 만들지 않고 같은 자리에서 갱신 (겹침 방지)
    if (!data.useCustomTable) {
        QRectF rect;
        double tableScale = 1.0;
        if (BeamScheduleTool::findTable(scene, rect, tableScale)) {
            BeamScheduleTool::buildTable(m_ctx, scene, data, rect.topLeft(), tableScale, true);
            if (m_ctx->commandDock())
                m_ctx->commandDock()->logMessage(QObject::tr("기존 보 일람표를 갱신했습니다."), CommandDock::Info);
            return;
        }
    }
    m_ctx->startTool(new BeamScheduleTool(data));
}
