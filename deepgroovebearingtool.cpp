#include "deepgroovebearingtool.h"
#include "bearinglayer.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "deepgroovebearingdialog.h"
#include "drawingsettingscommand.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "bearingrotation.h"
#include "tooldefaults.h"
#include "mechparttag.h"

#include <QKeyEvent>
#include <QPen>
#include <QTimer>
#include <QUndoStack>
#include <QtMath>

namespace {
const char *const kToolNamespace = "DeepGrooveBearingTool";
const char *const kLayerBody   = "BEARING";
const char *const kLayerCenter = "BEARING-CEN";
const char *const kLayerDim    = "BEARING-DIM";

// 둥근 모서리(필렛 r) 사각형: 왼쪽 위에서 시계 방향. 모서리마다 90° 호(bulge = -tan(90°/4))
// cTop = 위쪽 두 모서리 반지름, cBot = 아래쪽 두 모서리 반지름 (0 이면 직각)
std::vector<MyVector> chamferedRect(double x0, double y0, double x1, double y1,
                                    double cTop, double cBot, std::vector<double> &bulges) {
    std::vector<MyVector> v;
    bulges.clear();
    const double b = -qTan(M_PI / 8.0);
    // 모서리: 왼쪽 위 → 오른쪽 위 → 오른쪽 아래 → 왼쪽 아래 (시계 방향)
    // 각 모서리는 (호 시작점, 호 끝점) 2점 또는 직각이면 1점
    struct Corner { double cx, cy, c; double sx, sy, ex, ey; };
    const Corner corners[4] = {
        {x0, y1, cTop, x0, y1 - cTop, x0 + cTop, y1},
        {x1, y1, cTop, x1 - cTop, y1, x1, y1 - cTop},
        {x1, y0, cBot, x1, y0 + cBot, x1 - cBot, y0},
        {x0, y0, cBot, x0 + cBot, y0, x0, y0 + cBot},
    };
    for (int i = 0; i < 4; ++i) {
        const Corner &k = corners[i];
        if (k.c > 0.0) {
            v.push_back(MyVector(k.sx, k.sy));
            bulges.push_back(b);
            v.push_back(MyVector(k.ex, k.ey));
            bulges.push_back(0.0);
        } else {
            v.push_back(MyVector(k.cx, k.cy));
            bulges.push_back(0.0);
        }
    }
    return v;
}

BearingHalfShape makeHalf(const DeepGrooveBearingParams &p, const QPointF &o, bool upper) {
    BearingHalfShape h;
    const double rIn  = p.d * 0.5;
    const double rOut = p.D * 0.5;
    const double ringH = rOut - rIn;
    // 모따기가 폭/높이의 절반을 넘지 않게 제한
    const double c = qMin(p.r, qMin(p.B, ringH) * 0.4);

    const double xl = o.x() - p.B * 0.5;
    const double xr = o.x() + p.B * 0.5;
    double ya, yb;
    if (upper) { ya = o.y() + rIn;  yb = o.y() + rOut; }
    else       { ya = o.y() - rOut; yb = o.y() - rIn; }
    // 네 모서리 모두 반지름 r 필렛 (전체 그리기 여부와 무관)
    h.outline = chamferedRect(xl, ya, xr, yb, c, c, h.bulges);

    h.ballCenter = QPointF(o.x(), (ya + yb) * 0.5);
    // BE1.LSP 규칙: 볼 반지름 = (D-d)/6.8, 볼 중심 = 피치원 반지름 (d+D)/4, 폭의 가운데
    const double br = (p.D - p.d) / 6.8;
    h.ballRadius = br;

    // 볼받침(리테이너) 선: 볼의 45° 점에서 좌우 면까지 수평선 4개 (볼 위·아래 각 ±r·sin45°)
    const double s45 = br * qSin(M_PI / 4.0);
    const double c45 = br * qCos(M_PI / 4.0);
    const double cx = h.ballCenter.x();
    const double cy = h.ballCenter.y();
    for (int sy = -1; sy <= 1; sy += 2) {
        h.seatLines.append(QLineF(cx - c45, cy + sy * s45, xl, cy + sy * s45));
        h.seatLines.append(QLineF(cx + c45, cy + sy * s45, xr, cy + sy * s45));
    }
    // 볼 중심 십자선 (±1.3 r)
    h.crossH = QLineF(cx - br * 1.3, cy, cx + br * 1.3, cy);
    h.crossV = QLineF(cx, cy - br * 1.3, cx, cy + br * 1.3);

    // 해칭 영역 = 외형 - 볼받침 사이 틈(띠) - 볼
    QPainterPath outer;
    outer.addRoundedRect(QRectF(xl, ya, p.B, ringH), c, c);
    QPainterPath band;
    band.addRect(QRectF(xl - 1.0, cy - s45, p.B + 2.0, 2.0 * s45));
    QPainterPath ball;
    ball.addEllipse(h.ballCenter, br, br);
    h.hatchPath = outer.subtracted(band).subtracted(ball);
    return h;
}

MyDimension makeVerticalDim(const QPointF &top, const QPointF &bottom, double lineX) {
    MyDimension d{MyVector(top), MyVector(bottom), MyVector(QPointF(lineX, (top.y() + bottom.y()) * 0.5)),
                  QString()};
    d.dimensionType = DimensionType::Linear;
    d.useFixedAngle = true;
    d.fixedAngle    = 90.0;
    return d;
}

MyDimension makeHorizontalDim(const QPointF &a, const QPointF &b, double lineY) {
    MyDimension d{MyVector(a), MyVector(b), MyVector(QPointF((a.x() + b.x()) * 0.5, lineY)), QString()};
    d.dimensionType = DimensionType::Linear;
    d.useFixedAngle = true;
    d.fixedAngle    = 0.0;
    return d;
}

}

namespace {
void rotateGeometry(BearingGeometry &g, const QPointF &o, double deg) {
    if (qAbs(deg) < 1e-9) return;
    BearingRotation::Rot r(o, deg);
    BearingHalfShape *hs[2] = {&g.upper, &g.lower};
    for (int i = 0; i < 2; ++i) {
        BearingRotation::rotVecs(hs[i]->outline, r);
        hs[i]->ballCenter = r.pt(hs[i]->ballCenter);
        BearingRotation::rotLines(hs[i]->seatLines, r);
        BearingRotation::rotLine(hs[i]->crossH, r);
        BearingRotation::rotLine(hs[i]->crossV, r);
        BearingRotation::rotPath(hs[i]->hatchPath, r);
    }
    BearingRotation::rotLine(g.centerLine, r);
    BearingRotation::rotLines(g.connLines, r);
    BearingRotation::rotDims(g.dims, r, deg);
}
}

DeepGrooveBearingTool::DeepGrooveBearingTool(QObject *parent)
    : AbstractTool(parent)
{}

BearingGeometry DeepGrooveBearingTool::build(const DeepGrooveBearingParams &p, const QPointF &o) {
    BearingGeometry g;
    if (p.d <= 0.0 || p.D <= p.d || p.B <= 0.0) return g;

    g.upper = makeHalf(p, o, true);
    g.lower = makeHalf(p, o, false);

    const double rOut = p.D * 0.5;
    const double rIn  = p.d * 0.5;
    const double xl = o.x() - p.B * 0.5;
    const double xr = o.x() + p.B * 0.5;

    // 전체 그리기: 좌우 면에서 위·아래 단면을 잇는 수직 연결선 (구멍 면)
    // 선 끝은 구멍 쪽 필렛의 접점(안지름 ± r)이라 필렛 호와 매끄럽게 이어진다.
    if (p.drawFull) {
        const double c = qMin(p.r, qMin(p.B, rOut - rIn) * 0.4);
        g.connLines.append(QLineF(xl, o.y() + rIn + c, xl, o.y() - rIn - c));
        g.connLines.append(QLineF(xr, o.y() + rIn + c, xr, o.y() - rIn - c));
    }

    if (p.drawCenter) {
        g.hasCenterLine = true;
        const double ext = qMax(p.B * 0.3, 2.0);
        g.centerLine = QLineF(xl - ext, o.y(), xr + ext, o.y());
    }
    if (p.drawDims) {
        const double gap = qMax(p.D * 0.15, 4.0);
        // 폭 B : 위쪽
        g.dims.append(makeHorizontalDim(QPointF(xl, o.y() + rOut), QPointF(xr, o.y() + rOut),
                                        o.y() + rOut + gap));
        // ød : 오른쪽 안쪽 열, øD : 오른쪽 바깥 열
        MyDimension dd = makeVerticalDim(QPointF(xr, o.y() + rIn), QPointF(xr, o.y() - rIn), xr + gap);
        dd.prefix = QStringLiteral("Ø");
        g.dims.append(dd);
        MyDimension dD = makeVerticalDim(QPointF(xr, o.y() + rOut), QPointF(xr, o.y() - rOut), xr + gap * 2.0);
        dD.prefix = QStringLiteral("Ø");
        g.dims.append(dD);
    }
    return g;
}

QPainterPath DeepGrooveBearingTool::buildPath(const DeepGrooveBearingParams &p, const QPointF &o, double angleDeg) {
    QPainterPath path;
    BearingGeometry g = build(p, o);
    rotateGeometry(g, o, angleDeg);
    if (g.upper.outline.empty()) return path;
    path.addPath(g.upper.hatchPath);
    path.addPath(g.lower.hatchPath);
    path.addEllipse(g.upper.ballCenter, g.upper.ballRadius, g.upper.ballRadius);
    path.addEllipse(g.lower.ballCenter, g.lower.ballRadius, g.lower.ballRadius);
    const BearingHalfShape *hs[2] = {&g.upper, &g.lower};
    for (int i = 0; i < 2; ++i) {
        for (int k = 0; k < hs[i]->seatLines.size(); ++k) {
            path.moveTo(hs[i]->seatLines.at(k).p1());
            path.lineTo(hs[i]->seatLines.at(k).p2());
        }
    }
    for (int i = 0; i < g.connLines.size(); ++i) {
        path.moveTo(g.connLines.at(i).p1());
        path.lineTo(g.connLines.at(i).p2());
    }
    if (g.hasCenterLine) {
        path.moveTo(g.centerLine.p1());
        path.lineTo(g.centerLine.p2());
    }
    return path;
}

bool DeepGrooveBearingTool::s_headless = false;
DeepGrooveBearingParams DeepGrooveBearingTool::s_headlessParams;

void DeepGrooveBearingTool::setHeadlessParams(const DeepGrooveBearingParams &params) {
    s_headlessParams = params;
    s_headless = true;
}

void DeepGrooveBearingTool::clearHeadless() {
    s_headless = false;
}

void DeepGrooveBearingTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    if (s_headless) {               // 자동 배치(MCP): 설정창 없이 바로 위치 입력 단계
        m_params = s_headlessParams;
        showPositionPrompt();
        return;
    }

    ToolDefaults defaults(kToolNamespace, scene);
    DeepGrooveBearingParams saved;
    saved.name       = defaults.getString(QStringLiteral("Name"), saved.name);
    saved.d          = defaults.getDouble("D_in",  saved.d);
    saved.D          = defaults.getDouble("D_out", saved.D);
    saved.B          = defaults.getDouble("Width", saved.B);
    saved.r          = defaults.getDouble("R",     saved.r);
    saved.drawDims   = defaults.getBool("DrawDims",   saved.drawDims);
    saved.drawCenter = defaults.getBool("DrawCenter", saved.drawCenter);
    saved.drawHatch  = defaults.getBool("DrawHatch",  saved.drawHatch);
    saved.hatchScale = defaults.getDouble("HatchScale", 0.0);
    saved.drawFull   = defaults.getBool("DrawFull", saved.drawFull);

    DeepGrooveBearingDialog dlg(nullptr);
    dlg.setParams(saved);
    if (dlg.exec() != QDialog::Accepted) {
        // 취소: activate 시점에는 아직 toolFinished 시그널이 연결되기 전이라 바로 emit 하면
        // 무시되고 툴이 살아남아 기본값으로 베어링이 그려진다 → 입력을 막고 이벤트 루프로 종료를 미룬다.
        m_cancelled = true;
        QTimer::singleShot(0, this, &DeepGrooveBearingTool::finishTool);
        return;
    }
    m_params = dlg.params();
    defaults.put(QStringLiteral("Name"), m_params.name);
    defaults.put("D_in",  m_params.d);
    defaults.put("D_out", m_params.D);
    defaults.put("Width", m_params.B);
    defaults.put("R",     m_params.r);
    defaults.put("DrawDims",   m_params.drawDims);
    defaults.put("DrawCenter", m_params.drawCenter);
    defaults.put("DrawHatch",  m_params.drawHatch);
    defaults.put("HatchScale", m_params.hatchScale);
    defaults.put("DrawFull",   m_params.drawFull);

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("깊은 홈 볼베어링 %1 (d=%2, D=%3, B=%4, r=%5)")
                .arg(m_params.name).arg(m_params.d).arg(m_params.D).arg(m_params.B).arg(m_params.r),
            CommandDock::Command);
    }
    showPositionPrompt();
}

void DeepGrooveBearingTool::deactivate() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    AbstractTool::deactivate();
}

void DeepGrooveBearingTool::showPositionPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 베어링 중심 위치 지정 (Enter=종료): "), {});
    }
}

void DeepGrooveBearingTool::finishTool() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    emit toolFinished();
}

void DeepGrooveBearingTool::ensureLayer(IAppContext *ctx, const QString &name, const QColor &color,
                                        const QString &linetype) {
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, color, 0.25, linetype, false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

QGraphicsItem *DeepGrooveBearingTool::finalizeItem(QGraphicsItem *item, const QString &layer) {
    if (!item) return nullptr;
    IAppContext *ctx = getContext();
    if (CadItem *cad = dynamic_cast<CadItem *>(item)) cad->setLayerName(layer);
    item->setParentItem(m_scene->getLayerRoot(layer));
    if (item->scene() == m_scene) m_scene->removeItem(item);
    if (ctx && ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    else                         m_scene->addItem(item);
    return item;
}

void DeepGrooveBearingTool::placeAt(const QPointF &origin) {
    IAppContext *ctx = getContext();
    if (m_cancelled) return;
    if (!m_scene || !ctx || !ctx->entityFactory() || !ctx->layerManager()) return;

    BearingGeometry g = build(m_params, origin);
    rotateGeometry(g, origin, m_angleDeg);
    if (g.upper.outline.empty()) return;

    const QString bodyLayer = BearingLayer::bodyLayer(m_scene);
    ensureLayer(ctx, bodyLayer, QColor(255, 255, 255), QStringLiteral("Continuous"));

    QString centerLayer = DrawingSettingsCommand::s_centerLineLayer;
    if (centerLayer.isEmpty() || !ctx->layerManager()->getLayer(centerLayer)) {
        centerLayer = QString::fromLatin1(kLayerCenter);
        ensureLayer(ctx, centerLayer, QColor(255, 0, 0), QStringLiteral("CENTER"));
    }
    QString dimLayer = m_scene->dimensionStyle().dimLineLayer;
    if (dimLayer.isEmpty() || !ctx->layerManager()->getLayer(dimLayer)) {
        dimLayer = QString::fromLatin1(kLayerDim);
        ensureLayer(ctx, dimLayer, QColor(0, 255, 255), QStringLiteral("Continuous"));
    }

    // 해칭 레이어: 해칭 툴 설정 → 현재 레이어 → 외형 레이어
    QString hatchLayer = ToolDefaults("Hatch", m_scene).getString(QStringLiteral("Layer"), QString());
    if (hatchLayer.isEmpty() || !ctx->layerManager()->getLayer(hatchLayer)) {
        Layer *cur = ctx->layerManager()->getCurrentLayer();
        hatchLayer = cur ? cur->name() : bodyLayer;
    }

    const QPen bodyPen   = AbstractTool::layerPenFor(ctx, bodyLayer);
    const QPen centerPen = AbstractTool::layerPenFor(ctx, centerLayer);
    const QPen dimPen    = AbstractTool::layerPenFor(ctx, dimLayer);
    const QPen hatchPen  = AbstractTool::layerPenFor(ctx, hatchLayer);
    EntityFactory *ef = ctx->entityFactory();

    QUndoStack *undo = ctx->undoStack();
    if (undo) undo->beginMacro(tr("깊은 홈 볼베어링"));

    const BearingHalfShape *halves[2] = {&g.upper, &g.lower};
    for (int i = 0; i < 2; ++i) {
        const BearingHalfShape &h = *halves[i];
        QGraphicsItem *outlineItem = ef->createPolyline(h.outline, true, bodyPen, h.bulges);
        if (i == 0 && outlineItem) {
            // [3D 기계용] 규격을 저장하고 위쪽 단면 외형에 부품 UUID 를 붙인다
            QMap<QString, QVariant> props;
            props[QStringLiteral("Name")] = m_params.name;
            props[QStringLiteral("d")] = m_params.d;
            props[QStringLiteral("D")] = m_params.D;
            props[QStringLiteral("B")] = m_params.B;
            props[QStringLiteral("r")] = m_params.r;
            MechPartTag::registerPart(m_scene, undo, outlineItem, QStringLiteral("MechDeepGrooveBearing"),
                                      tr("베어링%1").arg(m_params.name.isEmpty() ? QString()
                                                                             : QStringLiteral(" ") + m_params.name),
                                      props, origin, m_angleDeg);
        }
        finalizeItem(outlineItem, bodyLayer);
        finalizeItem(ef->createCircle(h.ballCenter, h.ballRadius, bodyPen), bodyLayer);
        for (int k = 0; k < h.seatLines.size(); ++k)
            finalizeItem(ef->createLine(h.seatLines.at(k), bodyPen), bodyLayer);
        if (m_params.drawCenter) {
            finalizeItem(ef->createLine(h.crossH, centerPen), centerLayer);
            finalizeItem(ef->createLine(h.crossV, centerPen), centerLayer);
        }
        if (m_params.drawHatch)
            finalizeItem(ef->createHatch(h.hatchPath, QStringLiteral("ANSI31"),
                                      m_params.hatchScale > 0.0 ? m_params.hatchScale
                                          : DeepGrooveBearingTable::autoHatchScale(m_params.d, m_params.D),
                                      0.0, hatchPen),
                         hatchLayer);
    }
    for (int i = 0; i < g.connLines.size(); ++i)
        finalizeItem(ef->createLine(g.connLines.at(i), bodyPen), bodyLayer);
    if (g.hasCenterLine)
        finalizeItem(ef->createLine(g.centerLine, centerPen), centerLayer);
    for (int i = 0; i < g.dims.size(); ++i)
        finalizeItem(ef->createDimension(g.dims.at(i), dimPen), dimLayer);

    if (undo) undo->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("깊은 홈 볼베어링 %1 생성 완료.").arg(m_params.name),
                                       CommandDock::Info);
    }
    showPositionPrompt();
}

void DeepGrooveBearingTool::startAngleStep(const QPointF &origin) {
    m_baseOrigin = origin;
    m_awaitAngle = true;
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 회전 각도 지정 (클릭=방향, 각도 입력, Enter=0°, F8=직교, Esc=취소): "), {});
    }
}

void DeepGrooveBearingTool::finishAngleStep(double angleDeg) {
    if (!m_awaitAngle) return;
    m_awaitAngle = false;
    m_angleDeg = angleDeg;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    placeAt(m_baseOrigin);
    m_angleDeg = 0.0;
}

void DeepGrooveBearingTool::cancelAngleStep() {
    m_awaitAngle = false;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    showPositionPrompt();
}

double DeepGrooveBearingTool::angleFromPoint(const QPointF &p) const {
    const QPointF d = p - m_baseOrigin;
    if (qAbs(d.x()) < 1e-9 && qAbs(d.y()) < 1e-9) return 0.0;
    return qRadiansToDegrees(qAtan2(d.y(), d.x()));
}

bool DeepGrooveBearingTool::processCommand(const QString &command) {
    if (m_cancelled) return true;
    const QString cmd = command.trimmed();
    IAppContext *ctx = getContext();
    if (!ctx) return false;

    if (m_awaitAngle) {
        if (cmd.isEmpty()) {
            finishAngleStep(0.0);
            return true;
        }
        bool ok = false;
        const double ang = cmd.toDouble(&ok);
        if (ok) {
            finishAngleStep(ang);
            return true;
        }
        if (ctx->parser()) {
            QPointF resultPos;
            const bool ortho = ctx->snapManager() && ctx->snapManager()->isOrtho();
            if (ctx->parser()->parseCoordinate(cmd, m_baseOrigin, m_baseOrigin, ortho, resultPos)) {
                finishAngleStep(angleFromPoint(resultPos));
                return true;
            }
        }
        return false;
    }

    if (cmd.isEmpty()) {
        finishTool();
        return true;
    }
    if (ctx->parser()) {
        QPointF resultPos;
        if (ctx->parser()->parseCoordinate(cmd, QPointF(0, 0), QPointF(0, 0), false, resultPos)) {
            startAngleStep(resultPos);
            return true;
        }
    }
    return false;
}

void DeepGrooveBearingTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene || m_cancelled) return;
    if (button == Qt::RightButton) {
        if (m_awaitAngle) cancelAngleStep();
        else              finishTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    if (m_awaitAngle) {
        if (ctx && ctx->snapManager())
            snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, m_baseOrigin).snappedPos;
        finishAngleStep(angleFromPoint(snappedPos));
        return;
    }
    if (ctx && ctx->snapManager()) {
        snappedPos = ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;
    }
    startAngleStep(snappedPos);
}

void DeepGrooveBearingTool::mouseMove(QPointF pos) {
    if (!m_scene || m_cancelled) return;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    SnapType snapType = SnapType::None;
    if (m_awaitAngle) {
        // 기준점에서 커서 방향으로 회전한 모양을 미리 보여 준다 (F8 직교 적용).
        if (ctx && ctx->snapManager()) {
            const SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, m_baseOrigin);
            snappedPos = res.snappedPos;
            snapType = res.type;
        }
        m_scene->setSnapMarker(snappedPos, snapType);
        updateGhostPreview(buildPath(m_params, m_baseOrigin, angleFromPoint(snappedPos)),
                           currentLayerName());
        return;
    }
    if (ctx && ctx->snapManager()) {
        const SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, m_scene, QPointF());
        snappedPos = res.snappedPos;
        snapType = res.type;
    }
    m_scene->setSnapMarker(snappedPos, snapType);
    updateGhostPreview(buildPath(m_params, snappedPos), currentLayerName());
}

void DeepGrooveBearingTool::keyPress(QKeyEvent *event) {
    if (m_cancelled) return;
    if (m_awaitAngle) {
        if (event->key() == Qt::Key_Escape) {
            cancelAngleStep();
            return;
        }
        if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter ||
            event->key() == Qt::Key_Space) {
            finishAngleStep(0.0);
            return;
        }
        AbstractTool::keyPress(event);   // F8 등
        return;
    }
    if (event->key() == Qt::Key_Escape || event->key() == Qt::Key_Return ||
        event->key() == Qt::Key_Enter || event->key() == Qt::Key_Space) {
        finishTool();
        return;
    }
    AbstractTool::keyPress(event);
}

void DeepGrooveBearingTool::placeDirect(IAppContext *ctx, MyGraphicsScene *scene, const DeepGrooveBearingParams &params, const QPointF &origin,
                     double angleDeg) {
    if (!ctx || !scene) return;
    DeepGrooveBearingTool t;
    t.setContext(ctx);
    t.m_scene = scene;
    t.m_params = params;
    t.m_angleDeg = angleDeg;
    t.placeAt(origin);
}
