#include "taperedrollerbearingtool.h"
#include "bearinglayer.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "drawingsettingscommand.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "bearingrotation.h"
#include "taperedrollerbearingdialog.h"
#include "tooldefaults.h"

#include <QKeyEvent>
#include <QPen>
#include <QTimer>
#include <QUndoStack>
#include <QtMath>

namespace {
const char *const kToolNamespace = "TaperedRollerBearingTool";
const char *const kLayerBody   = "BEARING";
const char *const kLayerCenter = "BEARING-CEN";
const char *const kLayerDim    = "BEARING-DIM";

// 다각형 한 꼭짓점의 필렛 결과
struct FilletResult {
    std::vector<MyVector> pts;     // 폴리라인 꼭짓점 (필렛 호는 접점 2개 + bulge)
    std::vector<double>   bulges;
    QPainterPath          path;    // 호를 선분으로 근사한 해칭 경로
    QList<QPointF>        tanPrev; // 꼭짓점별: 이전 변 쪽 접점 (필렛 없으면 꼭짓점)
    QList<QPointF>        tanNext; // 꼭짓점별: 다음 변 쪽 접점
};

double crossOf(const QPointF &a, const QPointF &b) { return a.x() * b.y() - a.y() * b.x(); }
double dotOf(const QPointF &a, const QPointF &b)   { return a.x() * b.x() + a.y() * b.y(); }
double lenOf(const QPointF &a)                      { return qSqrt(dotOf(a, a)); }

// 임의 각도의 다각형 꼭짓점에 반지름 rad[i] 필렛을 넣는다 (0 이면 직각/뾰족 그대로).
// 접점은 이웃 변 길이의 45% 를 넘지 않게 제한한다.
FilletResult filletPolygon(const QList<QPointF> &v, const QList<double> &rad) {
    FilletResult res;
    const int n = v.size();
    for (int i = 0; i < n; ++i) {
        const QPointF prev = v.at((i + n - 1) % n);
        const QPointF next = v.at((i + 1) % n);
        const QPointF cur  = v.at(i);
        const QPointF e1 = prev - cur;
        const QPointF e2 = next - cur;
        const double l1 = lenOf(e1);
        const double l2 = lenOf(e2);
        const QPointF u = (l1 > 1e-9) ? e1 / l1 : QPointF(1, 0);
        const QPointF w = (l2 > 1e-9) ? e2 / l2 : QPointF(1, 0);
        double cosPhi = dotOf(u, w);
        if (cosPhi > 1.0) cosPhi = 1.0;
        if (cosPhi < -1.0) cosPhi = -1.0;
        const double phi = qAcos(cosPhi);   // 꼭짓점의 안쪽 각

        const double r = rad.at(i);
        const bool sharp = (r <= 0.0) || phi < 0.05 || phi > M_PI - 0.02;
        if (sharp) {
            res.pts.push_back(MyVector(cur));
            res.bulges.push_back(0.0);
            res.tanPrev.append(cur);
            res.tanNext.append(cur);
            if (res.path.isEmpty()) res.path.moveTo(cur);
            else                    res.path.lineTo(cur);
            continue;
        }

        double t = r / qTan(phi * 0.5);
        const double tMax = 0.45 * qMin(l1, l2);
        if (t > tMax) t = tMax;
        const double reff = t * qTan(phi * 0.5);
        const QPointF a = cur + u * t;
        const QPointF b = cur + w * t;
        const double turn = crossOf(cur - prev, next - cur);
        const double theta = M_PI - phi;   // 호가 도는 각
        const double bulge = qTan(theta * 0.25) * (turn >= 0.0 ? 1.0 : -1.0);

        res.pts.push_back(MyVector(a));
        res.bulges.push_back(bulge);
        res.pts.push_back(MyVector(b));
        res.bulges.push_back(0.0);
        res.tanPrev.append(a);
        res.tanNext.append(b);

        // 해칭 경로용 호 근사
        QPointF bis = u + w;
        const double lb = lenOf(bis);
        bis = (lb > 1e-9) ? bis / lb : QPointF(1, 0);
        const QPointF c = cur + bis * (reff / qSin(phi * 0.5));
        const double a0 = qAtan2(a.y() - c.y(), a.x() - c.x());
        const double a1 = qAtan2(b.y() - c.y(), b.x() - c.x());
        double delta = a1 - a0;
        while (delta > M_PI)   delta -= 2.0 * M_PI;
        while (delta <= -M_PI) delta += 2.0 * M_PI;
        const int steps = 8;
        if (res.path.isEmpty()) res.path.moveTo(a);
        else                    res.path.lineTo(a);
        for (int s = 1; s <= steps; ++s) {
            const double ang = a0 + delta * s / steps;
            res.path.lineTo(c.x() + reff * qCos(ang), c.y() + reff * qSin(ang));
        }
    }
    res.path.closeSubpath();
    return res;
}

TrbSection toSection(const FilletResult &f) {
    TrbSection s;
    s.outline   = f.pts;
    s.bulges    = f.bulges;
    s.hatchPath = f.path;
    return s;
}

// 로컬 좌표(컵 후면 x=0, 축선 y=0) → 씬 좌표
struct Frame {
    double ox;
    double oy;
    QPointF at(double x, double y) const { return QPointF(ox + x, oy + y); }
};

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

void addSectionPath(QPainterPath &path, const TrbSection &s) {
    path.addPath(s.hatchPath);
}
}

namespace {
void rotateGeometry(TaperedRollerBearingGeometry &g, const QPointF &o, double deg) {
    if (qAbs(deg) < 1e-9) return;
    BearingRotation::Rot r(o, deg);
    TrbSection *secs[4] = {&g.cupU, &g.coneU, &g.cupL, &g.coneL};
    for (int i = 0; i < 4; ++i) {
        BearingRotation::rotVecs(secs[i]->outline, r);
        BearingRotation::rotPath(secs[i]->hatchPath, r);
    }
    BearingRotation::rotLines(g.lines, r);
    BearingRotation::rotLines(g.axisLines, r);
    BearingRotation::rotLine(g.centerLine, r);
    BearingRotation::rotDims(g.dims, r, deg);
}
}

TaperedRollerBearingTool::TaperedRollerBearingTool(QObject *parent)
    : AbstractTool(parent)
{}

// BE2.LSP 의 좌표 규칙을 그대로 옮긴 도형 계산.
//   컵 후면 x=0, 콘 후면 x=T, 롤러 중심 (T/2, d/2+(D-d)/4), 경사각 α(기본 15°).
//   컵 레이스 = 롤러 윗변, 콘 레이스 = 롤러 아랫변 (모두 α 기울기).
TaperedRollerBearingGeometry TaperedRollerBearingTool::build(const TaperedRollerBearingParams &p,
                                                             const QPointF &o) {
    TaperedRollerBearingGeometry g;
    if (p.d <= 0.0 || p.D <= p.d || p.B <= 0.0 || p.T <= 0.0 || p.C <= 0.0) return g;

    const Frame f{o.x() - p.T * 0.5, o.y()};
    const double alpha = qDegreesToRadians(p.angle);
    const double tanA = qTan(alpha);
    const double cosA = qCos(alpha);
    const double sinA = qSin(alpha);

    const double rIn  = p.d * 0.5;
    const double rOut = p.D * 0.5;
    const double xc = p.T * 0.5;
    const double yc = rIn + (p.D - p.d) * 0.25;    // 롤러 중심 높이 (피치)
    const double v  = (p.D - p.d) * 0.125;         // 롤러 수직 반높이 (BE2 bl14)
    const double h  = v * cosA;                    // 롤러 수직 반폭 (BE2 bl15)

    // 컵 레이스(롤러 윗변)와 콘 레이스(롤러 아랫변)의 높이
    const double yCupL  = yc + v + (0.0 - xc) * tanA;
    const double yCupR  = yc + v + (p.C - xc) * tanA;
    const double yConeF = yc - v + ((p.T - p.B) - xc) * tanA;   // 콘 앞면 x=T-B
    const double yConeR = yc - v + (p.C - xc) * tanA;           // 컵 앞면 x=C 위치

    // 위 단면(+y) 다각형, 아래 단면은 y 부호만 반대로
    const int kSign[2] = {1, -1};
    FilletResult cupFill[2];
    FilletResult coneFill[2];
    for (int k = 0; k < 2; ++k) {
        const double s = kSign[k];
        QList<QPointF> cup;
        cup << f.at(0.0, s * rOut) << f.at(p.C, s * rOut) << f.at(p.C, s * yCupR) << f.at(0.0, s * yCupL);
        QList<double> cupRad;
        cupRad << p.r << p.r << p.r << p.r;
        cupFill[k] = filletPolygon(cup, cupRad);

        QList<QPointF> cone;
        cone << f.at(p.T - p.B, s * rIn) << f.at(p.T, s * rIn) << f.at(p.T, s * yConeR)
             << f.at(p.C, s * yConeR) << f.at(p.T - p.B, s * yConeF);
        QList<double> coneRad;
        coneRad << p.R << p.R << p.R << 0.0 << p.R;
        coneFill[k] = filletPolygon(cone, coneRad);
    }
    g.cupU = toSection(cupFill[0]);
    g.cupL = toSection(cupFill[1]);
    g.coneU = toSection(coneFill[0]);
    g.coneL = toSection(coneFill[1]);

    // 롤러: 양 끝선 (긴 변은 컵·콘 레이스선과 일치). 작은 쪽(왼쪽) 0.45C, 큰 쪽(오른쪽) 0.25C
    for (int k = 0; k < 2; ++k) {
        const double s = kSign[k];
        const QPointF ctr = f.at(xc, s * yc);
        // 롤러 축 방향 (α, 아래 단면은 거울상)과 수직 방향
        const QPointF axis(cosA, s * sinA);
        const QPointF perp(-s * sinA, cosA);     // 위 단면: (-sinα, cosα)
        const QPointF perpOut = perp * (s > 0 ? 1.0 : -1.0);   // 항상 베어링 바깥쪽(+|y|) 방향
        const QPointF left  = ctr - axis * (0.45 * p.C);
        const QPointF right = ctr + axis * (0.25 * p.C);
        g.lines.append(QLineF(left + perpOut * h, left - perpOut * h));
        g.lines.append(QLineF(right + perpOut * h, right - perpOut * h));
        // 롤러 축선 (왼쪽 0.55C ~ 오른쪽 0.35C)
        g.axisLines.append(QLineF(ctr - axis * (0.55 * p.C), ctr + axis * (0.35 * p.C)));

        // 컵 앞면(x=C) 틈선: 컵 앞면 아래 끝(필렛 접점)에서 콘 레이스까지
        g.lines.append(QLineF(cupFill[k].tanPrev.at(2), f.at(p.C, s * yConeR)));
    }

    // 전체 그리기: 면선을 베어링 전체 높이로 이어 위·아래 단면을 연결 (BE2.LSP 방식)
    if (p.drawFull) {
        // 컵 후면 x=0 : 컵 4번 꼭짓점의 위쪽 면 접점
        g.lines.append(QLineF(cupFill[0].tanNext.at(3), cupFill[1].tanNext.at(3)));
        // 콘 앞면 x=T-B : 콘 1번 꼭짓점의 앞면 접점
        g.lines.append(QLineF(coneFill[0].tanPrev.at(0), coneFill[1].tanPrev.at(0)));
        // 콘 후면 x=T : 콘 2번 꼭짓점의 후면 접점
        g.lines.append(QLineF(coneFill[0].tanNext.at(1), coneFill[1].tanNext.at(1)));
    }

    if (p.drawCenter) {
        g.hasCenterLine = true;
        g.centerLine = QLineF(f.at(-0.15 * p.T, 0.0), f.at(p.T + 0.15 * p.T, 0.0));
    }

    if (p.drawDims) {
        const double gap = qMax(p.D * 0.15, 4.0);
        // 위쪽: C(안쪽 줄), T(바깥 줄)
        g.dims.append(makeHorizontalDim(f.at(0.0, rOut), f.at(p.C, rOut), f.at(0.0, rOut + gap).y()));
        g.dims.append(makeHorizontalDim(f.at(0.0, rOut), f.at(p.T, rOut), f.at(0.0, rOut + gap * 1.8).y()));
        // 아래쪽: B (콘 폭)
        g.dims.append(makeHorizontalDim(f.at(p.T - p.B, -rIn), f.at(p.T, -rIn), f.at(0.0, -rOut - gap).y()));
        // 왼쪽: D (컵 바깥지름), 오른쪽: d (콘 안지름)
        MyDimension dD = makeVerticalDim(f.at(0.0, rOut), f.at(0.0, -rOut), f.at(-gap, 0.0).x());
        dD.prefix = QStringLiteral("Ø");
        g.dims.append(dD);
        MyDimension dd = makeVerticalDim(f.at(p.T, rIn), f.at(p.T, -rIn), f.at(p.T + gap, 0.0).x());
        dd.prefix = QStringLiteral("Ø");
        g.dims.append(dd);
    }
    return g;
}

QPainterPath TaperedRollerBearingTool::buildPath(const TaperedRollerBearingParams &p, const QPointF &o, double angleDeg) {
    QPainterPath path;
    TaperedRollerBearingGeometry g = build(p, o);
    rotateGeometry(g, o, angleDeg);
    if (g.cupU.outline.empty()) return path;
    addSectionPath(path, g.cupU);
    addSectionPath(path, g.coneU);
    addSectionPath(path, g.cupL);
    addSectionPath(path, g.coneL);
    for (int i = 0; i < g.lines.size(); ++i) {
        path.moveTo(g.lines.at(i).p1());
        path.lineTo(g.lines.at(i).p2());
    }
    for (int i = 0; i < g.axisLines.size(); ++i) {
        path.moveTo(g.axisLines.at(i).p1());
        path.lineTo(g.axisLines.at(i).p2());
    }
    if (g.hasCenterLine) {
        path.moveTo(g.centerLine.p1());
        path.lineTo(g.centerLine.p2());
    }
    return path;
}

void TaperedRollerBearingTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    ToolDefaults defaults(kToolNamespace, scene);
    TaperedRollerBearingParams saved;
    saved.name       = defaults.getString(QStringLiteral("Name"), saved.name);
    saved.d          = defaults.getDouble("D_in",  saved.d);
    saved.D          = defaults.getDouble("D_out", saved.D);
    saved.B          = defaults.getDouble("ConeWidth", saved.B);
    saved.T          = defaults.getDouble("TotalWidth", saved.T);
    saved.C          = defaults.getDouble("CupWidth", saved.C);
    saved.r          = defaults.getDouble("CupR",  saved.r);
    saved.R          = defaults.getDouble("ConeR", saved.R);
    saved.angle      = defaults.getDouble("Angle", saved.angle);
    saved.drawDims   = defaults.getBool("DrawDims",   saved.drawDims);
    saved.drawCenter = defaults.getBool("DrawCenter", saved.drawCenter);
    saved.drawHatch  = defaults.getBool("DrawHatch",  saved.drawHatch);
    saved.hatchScale = defaults.getDouble("HatchScale", 0.0);
    saved.drawFull   = defaults.getBool("DrawFull", saved.drawFull);

    TaperedRollerBearingDialog dlg(nullptr);
    dlg.setParams(saved);
    if (dlg.exec() != QDialog::Accepted) {
        // 취소: activate 시점엔 toolFinished 가 아직 연결 전이라 바로 emit 하면 무시된다.
        // 입력을 막고 이벤트 루프로 종료를 미룬다.
        m_cancelled = true;
        QTimer::singleShot(0, this, &TaperedRollerBearingTool::finishTool);
        return;
    }
    m_params = dlg.params();
    defaults.put(QStringLiteral("Name"), m_params.name);
    defaults.put("D_in",  m_params.d);
    defaults.put("D_out", m_params.D);
    defaults.put("ConeWidth",  m_params.B);
    defaults.put("TotalWidth", m_params.T);
    defaults.put("CupWidth",   m_params.C);
    defaults.put("CupR",  m_params.r);
    defaults.put("ConeR", m_params.R);
    defaults.put("Angle", m_params.angle);
    defaults.put("DrawDims",   m_params.drawDims);
    defaults.put("DrawCenter", m_params.drawCenter);
    defaults.put("DrawHatch",  m_params.drawHatch);
    defaults.put("HatchScale", m_params.hatchScale);
    defaults.put("DrawFull",   m_params.drawFull);

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("테이퍼롤러베어링 %1 (d=%2, D=%3, B=%4, T=%5, C=%6)")
                .arg(m_params.name).arg(m_params.d).arg(m_params.D)
                .arg(m_params.B).arg(m_params.T).arg(m_params.C),
            CommandDock::Command);
    }
    showPositionPrompt();
}

void TaperedRollerBearingTool::deactivate() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    AbstractTool::deactivate();
}

void TaperedRollerBearingTool::showPositionPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 베어링 중심 위치 지정 (Enter=종료): "), {});
    }
}

void TaperedRollerBearingTool::finishTool() {
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    emit toolFinished();
}

void TaperedRollerBearingTool::ensureLayer(IAppContext *ctx, const QString &name, const QColor &color,
                                           const QString &linetype) {
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, color, 0.25, linetype, false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

QGraphicsItem *TaperedRollerBearingTool::finalizeItem(QGraphicsItem *item, const QString &layer) {
    if (!item) return nullptr;
    IAppContext *ctx = getContext();
    if (CadItem *cad = dynamic_cast<CadItem *>(item)) cad->setLayerName(layer);
    item->setParentItem(m_scene->getLayerRoot(layer));
    if (item->scene() == m_scene) m_scene->removeItem(item);
    if (ctx && ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(m_scene, item));
    else                         m_scene->addItem(item);
    return item;
}

void TaperedRollerBearingTool::placeAt(const QPointF &origin) {
    IAppContext *ctx = getContext();
    if (m_cancelled) return;
    if (!m_scene || !ctx || !ctx->entityFactory() || !ctx->layerManager()) return;

    TaperedRollerBearingGeometry g = build(m_params, origin);
    rotateGeometry(g, origin, m_angleDeg);
    if (g.cupU.outline.empty()) return;

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

    const double hatchScale = m_params.hatchScale > 0.0
        ? m_params.hatchScale
        : TaperedRollerBearingTable::autoHatchScale(m_params.d, m_params.D);

    QUndoStack *undo = ctx->undoStack();
    if (undo) undo->beginMacro(tr("테이퍼롤러베어링"));

    const TrbSection *sections[4] = {&g.cupU, &g.coneU, &g.cupL, &g.coneL};
    for (int i = 0; i < 4; ++i) {
        const TrbSection &s = *sections[i];
        finalizeItem(ef->createPolyline(s.outline, true, bodyPen, s.bulges), bodyLayer);
        if (m_params.drawHatch)
            finalizeItem(ef->createHatch(s.hatchPath, QStringLiteral("ANSI31"), hatchScale, 0.0, hatchPen),
                         hatchLayer);
    }
    for (int i = 0; i < g.lines.size(); ++i)
        finalizeItem(ef->createLine(g.lines.at(i), bodyPen), bodyLayer);
    if (m_params.drawCenter) {
        for (int i = 0; i < g.axisLines.size(); ++i)
            finalizeItem(ef->createLine(g.axisLines.at(i), centerPen), centerLayer);
        if (g.hasCenterLine)
            finalizeItem(ef->createLine(g.centerLine, centerPen), centerLayer);
    }
    for (int i = 0; i < g.dims.size(); ++i)
        finalizeItem(ef->createDimension(g.dims.at(i), dimPen), dimLayer);

    if (undo) undo->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(tr("테이퍼롤러베어링 %1 생성 완료.").arg(m_params.name),
                                       CommandDock::Info);
    }
    showPositionPrompt();
}

void TaperedRollerBearingTool::startAngleStep(const QPointF &origin) {
    m_baseOrigin = origin;
    m_awaitAngle = true;
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(
            tr(" 회전 각도 지정 (클릭=방향, 각도 입력, Enter=0°, F8=직교, Esc=취소): "), {});
    }
}

void TaperedRollerBearingTool::finishAngleStep(double angleDeg) {
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

void TaperedRollerBearingTool::cancelAngleStep() {
    m_awaitAngle = false;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    showPositionPrompt();
}

double TaperedRollerBearingTool::angleFromPoint(const QPointF &p) const {
    const QPointF d = p - m_baseOrigin;
    if (qAbs(d.x()) < 1e-9 && qAbs(d.y()) < 1e-9) return 0.0;
    return qRadiansToDegrees(qAtan2(d.y(), d.x()));
}

bool TaperedRollerBearingTool::processCommand(const QString &command) {
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

void TaperedRollerBearingTool::mousePress(QPointF pos, Qt::MouseButton button) {
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

void TaperedRollerBearingTool::mouseMove(QPointF pos) {
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

void TaperedRollerBearingTool::keyPress(QKeyEvent *event) {
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

void TaperedRollerBearingTool::placeDirect(IAppContext *ctx, MyGraphicsScene *scene, const TaperedRollerBearingParams &params, const QPointF &origin,
                     double angleDeg) {
    if (!ctx || !scene) return;
    TaperedRollerBearingTool t;
    t.setContext(ctx);
    t.m_scene = scene;
    t.m_params = params;
    t.m_angleDeg = angleDeg;
    t.placeAt(origin);
}
