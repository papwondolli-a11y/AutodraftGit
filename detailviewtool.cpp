#include "detailviewtool.h"

#include "cadcommands.h"
#include "blockitem.h"
#include "caditem.h"
#include "commanddock.h"
#include "detailregiondata.h"
#include "detailviewsetupdialog.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layer.h"
#include "layermanager.h"
#include "myarc.h"
#include "mycircle.h"
#include "mydimension.h"
#include "myentity.h"
#include "mygraphicsscene.h"
#include "myhatch.h"
#include "myleader.h"
#include "myline.h"
#include "mymath.h"
#include "mypolyline.h"
#include "myvector.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <algorithm>
#include <cmath>

#include <QFont>
#include <QKeyEvent>
#include <QLineF>
#include <QPainterPath>
#include <QPen>
#include <QPair>
#include <QPolygonF>
#include <QSet>
#include <QTransform>
#include <QUndoStack>
#include <QWidget>
#include <QtMath>

// 참조 마커(경계원 + 라벨)가 놓이는 레이어 이름
static const QString kMarkerLayer = QStringLiteral("DetailMarker");

// 축척 기본값 (2:1 확대)
static const double kDefaultScale = 2.0;
static const double kEps = 1e-6;

// ── Setup 설정 정적 저장소 ────────────────────────────────────────────────
QString DetailViewTool::s_detailLayer;
double  DetailViewTool::s_labelTextSize = 0.0;   // 0 = 도면 설정값 사용
bool    DetailViewTool::s_labelOnLeader = true;
bool    DetailViewTool::s_labelOnBoundary = false;
QString DetailViewTool::s_scaleText = QStringLiteral("2:1");

// 소수점 뒤 불필요한 0 제거 ("2.000" → "2", "1.500" → "1.5")
static QString trimTrailingZeros(double v) {
    QString s = QString::number(v, 'f', 3);
    if (s.contains(QLatin1Char('.'))) {
        while (s.endsWith(QLatin1Char('0')))
            s.chop(1);
        if (s.endsWith(QLatin1Char('.')))
            s.chop(1);
    }
    return s;
}

DetailViewTool::DetailViewTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitCenter), m_radius(0.0),
      m_scale(kDefaultScale) {}

DetailViewTool::~DetailViewTool() { cancelDrawing(); }

void DetailViewTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene)
        return;

    AbstractTool::activate(scene);
    loadSettings(scene); // [3-2] 매 활성화마다 현재 도면값 반영
    m_state = WaitCenter;
    m_radius = 0.0;
    m_scale = parseScaleRatio(s_scaleText, kDefaultScale);

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("상세도(DETAIL)"),
                                       CommandDock::Command);
    }
    showCenterPrompt();
}

void DetailViewTool::deactivate() {
    cancelDrawing();
    AbstractTool::deactivate();
}

// ──────────────────────────────────────────────────────────────
// 프롬프트
// ──────────────────────────────────────────────────────────────
void DetailViewTool::showCenterPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(
            tr(" 상세도로 만들 영역의 중심점을 지정하세요 <%1> : ")
                .arg(formatScaleRatio(m_scale)),
            {"Setup"});
}

void DetailViewTool::showRadiusPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(
            tr(" 영역 반지름을 지정하세요 : "), {});
}

void DetailViewTool::showScalePrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        QStringList options;
        options << "2:1" << "5:1" << "10:1";
        ctx->commandDock()->showInteractivePrompt(
            tr(" 상세도 축척 배율 입력 <%1> : ")
                .arg(trimTrailingZeros(m_scale)),
            options);
    }
}

void DetailViewTool::showPlacePrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->showInteractivePrompt(
            tr(" 상세도를 배치할 위치를 지정하세요 : "), {});
}

// ──────────────────────────────────────────────────────────────
// 입력 처리
// ──────────────────────────────────────────────────────────────
QPointF DetailViewTool::snappedPoint(const QPointF &pos,
                                     const QPointF &base) const {
    IAppContext *ctx = getContext();
    if (ctx && ctx->snapManager() && m_scene)
        return ctx->snapManager()->snapPoint(pos, m_scene, base).snappedPos;
    return pos;
}

bool DetailViewTool::processCommand(const QString &command) {
    QString cmd = command.trimmed();
    IAppContext *ctx = getContext();
    if (!ctx)
        return false;

    if (m_state == WaitCenter) {
        const QString lower = cmd.toLower();
        if (lower == "s" || lower == "setup") {
            openSetupDialog();
            return true;
        }
        if (ctx->parser()) {
            QPointF resultPos;
            if (ctx->parser()->parseCoordinate(cmd, QPointF(0, 0), QPointF(0, 0),
                                               false, resultPos)) {
                mousePress(resultPos, Qt::LeftButton);
                return true;
            }
        }
        return false;
    }

    if (m_state == WaitRadius) {
        bool ok = false;
        double val = cmd.toDouble(&ok);
        if (ok && val > kEps) {
            m_radius = val;
            m_state = WaitScale;
            if (m_scene)
                m_scene->setGhostPath(QPainterPath(), false);
            showScalePrompt();
            return true;
        }
        if (ctx->parser()) {
            QPointF resultPos;
            if (ctx->parser()->parseCoordinate(cmd, m_center, m_center, false,
                                               resultPos)) {
                mousePress(resultPos, Qt::LeftButton);
                return true;
            }
        }
        return false;
    }

    if (m_state == WaitScale) {
        if (cmd.isEmpty()) { // Enter → 기본값 유지
            m_state = WaitPlace;
            showPlacePrompt();
            return true;
        }
        double factor = 0.0;
        bool ok = false;
        if (cmd.contains(QLatin1Char(':'))) {
            const QStringList parts = cmd.split(QLatin1Char(':'));
            if (parts.size() == 2) {
                bool okA = false, okB = false;
                double a = parts.at(0).trimmed().toDouble(&okA);
                double b = parts.at(1).trimmed().toDouble(&okB);
                if (okA && okB && qAbs(b) > kEps) {
                    factor = a / b;
                    ok = true;
                }
            }
        } else {
            factor = cmd.toDouble(&ok);
        }
        if (ok && factor > kEps) {
            m_scale = factor;
            m_state = WaitPlace;
            showPlacePrompt();
            return true;
        }
        return false;
    }

    if (m_state == WaitPlace) {
        if (ctx->parser()) {
            QPointF resultPos;
            if (ctx->parser()->parseCoordinate(cmd, m_center, m_center, false,
                                               resultPos)) {
                mousePress(resultPos, Qt::LeftButton);
                return true;
            }
        }
        return false;
    }

    return false;
}

void DetailViewTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene || button != Qt::LeftButton)
        return;

    if (m_state == WaitCenter) {
        m_center = snappedPoint(pos, QPointF());
        m_currentPos = m_center;
        m_state = WaitRadius;
        showRadiusPrompt();
        return;
    }

    if (m_state == WaitRadius) {
        QPointF edge = snappedPoint(pos, m_center);
        double r = QLineF(m_center, edge).length();
        if (r > kEps) {
            m_radius = r;
            m_state = WaitScale;
            m_scene->setGhostPath(QPainterPath(), false);
            showScalePrompt();
        }
        return;
    }

    if (m_state == WaitScale) {
        // 클릭으로는 배율을 정할 수 없으므로 현재값(기본/직전값)으로 진행
        m_state = WaitPlace;
        showPlacePrompt();
        return;
    }

    if (m_state == WaitPlace) {
        QPointF place = snappedPoint(pos, m_center);
        buildDetailView(place);
    }
}

void DetailViewTool::mouseMove(QPointF pos) {
    if (!m_scene || m_scene->views().isEmpty())
        return;

    IAppContext *ctx = getContext();
    QPointF base = (m_state == WaitCenter) ? QPointF() : m_center;
    QPointF snapped = snappedPoint(pos, base);
    m_currentPos = snapped;

    if (m_state == WaitRadius)
        updateRadiusGhost(snapped);
    else if (m_state == WaitPlace)
        updatePlaceGhost(snapped);

    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager())
        snapType = ctx->snapManager()->snapPoint(pos, m_scene, base).type;
    m_scene->setSnapMarker(snapped, snapType);
}

void DetailViewTool::updateRadiusGhost(const QPointF &edgePos) {
    double r = QLineF(m_center, edgePos).length();
    if (r <= kEps)
        return;
    QPainterPath path;
    path.addEllipse(m_center, r, r);
    updateGhostPreview(path, currentLayerName());
}

void DetailViewTool::updatePlaceGhost(const QPointF &placePos) {
    double r = m_radius * m_scale;
    if (r <= kEps)
        return;
    QPainterPath path;
    path.addEllipse(placePos, r, r);
    double c = r * 0.15;
    path.moveTo(placePos.x() - c, placePos.y());
    path.lineTo(placePos.x() + c, placePos.y());
    path.moveTo(placePos.x(), placePos.y() - c);
    path.lineTo(placePos.x(), placePos.y() + c);
    updateGhostPreview(path, currentLayerName());
}

// ──────────────────────────────────────────────────────────────
// 영역 내 객체 수집
// ──────────────────────────────────────────────────────────────
QList<CadItem *> DetailViewTool::collectItemsInCircle(const QPointF &center,
                                                      double radius) const {
    QList<CadItem *> result;
    if (!m_scene || radius <= kEps)
        return result;

    QRectF box(center.x() - radius, center.y() - radius, radius * 2.0,
               radius * 2.0);
    QPainterPath circlePath;
    circlePath.addEllipse(center, radius, radius);

    const QList<QGraphicsItem *> raw =
        m_scene->items(box, Qt::IntersectsItemBoundingRect);
    const QList<CadItem *> tops = getTopLevelCadItems(raw);

    for (CadItem *ci : tops) {
        if (!ci)
            continue;
        // 이미 만들어진 상세도 내용·참조 마커·경계원은 원본 대상에서 제외
        if (ci->data(DETAIL_REGION_ROLE).isValid() ||
            ci->data(DETAIL_REGION_LABEL).isValid())
            continue;
        if (ci->layerName() == kMarkerLayer)
            continue;
        if (ci->layerName().startsWith(QLatin1String("DETAIL-")))
            continue;

        QPainterPath geom = ci->mapToScene(ci->geometryPath());
        if (geom.isEmpty()) {
            QPainterPath bp;
            bp.addPolygon(ci->mapToScene(ci->boundingRect()));
            if (circlePath.intersects(bp))
                result.append(ci);
            continue;
        }
        if (circlePath.intersects(geom) ||
            circlePath.contains(geom.boundingRect()))
            result.append(ci);
    }
    return result;
}

// ──────────────────────────────────────────────────────────────
// 좌표 변환
// ──────────────────────────────────────────────────────────────
QPointF DetailViewTool::mapPoint(const MyVector &p) const {
    return QPointF((p.x - m_center.x()) * m_scale + m_place.x(),
                   (p.y - m_center.y()) * m_scale + m_place.y());
}

QPainterPath DetailViewTool::mapPath(const QPainterPath &src) const {
    QTransform t;
    t.translate(m_place.x(), m_place.y());
    t.scale(m_scale, m_scale);
    t.translate(-m_center.x(), -m_center.y());
    return t.map(src);
}

bool DetailViewTool::insideBoundary(const MyVector &p) const {
    double dx = p.x - m_center.x();
    double dy = p.y - m_center.y();
    return (dx * dx + dy * dy) <= (m_radius * m_radius) + m_radius * 1e-4 + kEps;
}

// ──────────────────────────────────────────────────────────────
// 엔티티 클리핑
// ──────────────────────────────────────────────────────────────
void DetailViewTool::emitClippedItem(CadItem *src, const QPen &pen,
                                     QList<QGraphicsItem *> &out) const {
    if (!src)
        return;
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->entityFactory())
        return;

    // BlockItem: getEntityData()의 타입이 더미(Line)라 아래 switch로는 못 잡는다.
    // 블록은 논리 단위라 분할하지 않고 통째로 복제 → 상세도 좌표계로 변환한다.
    // (collectItemsInCircle 이 이미 경계원과 겹치는 것만 넘겨준다)
    if (dynamic_cast<BlockItem *>(src)) {
        CadItem *c = src->clone();
        if (c) {
            c->move(MyVector(-m_center.x(), -m_center.y()));
            c->scale(MyVector(0.0, 0.0), m_scale);
            c->move(MyVector(m_place.x(), m_place.y()));
            out.append(c);
        }
        return;
    }

    const MyEntity *e = src->getEntityData();
    if (!e) {
        cloneAtomicIfInside(src, out);
        return;
    }

    switch (e->type()) {
    case EntityType::Line:
        clipLineToBoundary(*static_cast<const MyLine *>(e), pen, out);
        break;
    case EntityType::Circle:
        clipCircleToBoundary(*static_cast<const MyCircle *>(e), pen, out);
        break;
    case EntityType::Arc:
        clipArcToBoundary(*static_cast<const MyArc *>(e), pen, out);
        break;
    case EntityType::Polyline:
        clipPolylineToBoundary(*static_cast<const MyPolyline *>(e), pen, out);
        break;
    case EntityType::Dimension:
        emitDimensionIfContained(src, out);
        break;
    case EntityType::Hatch: {
        const MyHatch *h = static_cast<const MyHatch *>(e);
        QPainterPath bnd;
        bnd.addEllipse(m_center, m_radius, m_radius);
        QPainterPath clipped = h->path.intersected(bnd);
        if (!clipped.isEmpty()) {
            QPainterPath tp = mapPath(clipped);
            QGraphicsItem *item =
                h->patternName.isEmpty()
                    ? ctx->entityFactory()->createHatch(tp, h->style, pen)
                    : ctx->entityFactory()->createHatch(
                          tp, h->patternName, h->patternScale, h->patternAngle,
                          pen);
            if (item)
                out.append(item);
        }
        break;
    }
    case EntityType::Spline: {
        // 스플라인은 근사 폴리라인으로 클리핑
        const QList<QPolygonF> polys =
            src->geometryPath().toSubpathPolygons();
        for (const QPolygonF &poly : polys) {
            std::vector<MyVector> pts;
            pts.reserve(poly.size());
            for (const QPointF &pp : poly)
                pts.push_back(MyVector(pp));
            clipPointRunToBoundary(pts, false, pen, out);
        }
        break;
    }
    default:
        // Text/Dimension/Leader/GDT/표면기호/Hole 등: 통째 포함 or 제외
        cloneAtomicIfInside(src, out);
        break;
    }
}

void DetailViewTool::clipLineToBoundary(const MyLine &line, const QPen &pen,
                                        QList<QGraphicsItem *> &out) const {
    IAppContext *ctx = getContext();
    MyVector d = line.end - line.start;
    double len2 = d.x * d.x + d.y * d.y;
    if (len2 < kEps)
        return;

    MyCircle boundary(MyVector(m_center), m_radius);
    std::vector<MyVector> hits = MyMath::intersectLineCircle(line, boundary);

    std::vector<double> ts;
    ts.push_back(0.0);
    ts.push_back(1.0);
    for (const MyVector &h : hits) {
        double t = ((h.x - line.start.x) * d.x + (h.y - line.start.y) * d.y) /
                   len2;
        if (t > kEps && t < 1.0 - kEps)
            ts.push_back(t);
    }
    std::sort(ts.begin(), ts.end());

    for (size_t i = 0; i + 1 < ts.size(); ++i) {
        double ta = ts[i], tb = ts[i + 1];
        if (tb - ta < kEps)
            continue;
        MyVector mid = line.start + d * (0.5 * (ta + tb));
        if (!insideBoundary(mid))
            continue;
        MyVector a = line.start + d * ta;
        MyVector b = line.start + d * tb;
        QGraphicsItem *item = ctx->entityFactory()->createLine(
            QLineF(mapPoint(a), mapPoint(b)), pen);
        if (item)
            out.append(item);
    }
}

void DetailViewTool::clipArcToBoundary(const MyArc &arc, const QPen &pen,
                                       QList<QGraphicsItem *> &out) const {
    IAppContext *ctx = getContext();
    double span = arc.spanAngle;
    if (std::abs(span) < kEps)
        return;

    MyCircle boundary(MyVector(m_center), m_radius);
    std::vector<MyVector> hits = MyMath::intersectCircleArc(boundary, arc);

    std::vector<double> ts;
    ts.push_back(0.0);
    ts.push_back(1.0);
    for (const MyVector &h : hits) {
        double ang = MyMath::rad2deg(
            std::atan2(h.y - arc.center.y, h.x - arc.center.x));
        double off = MyMath::normalizeAngle(ang - arc.startAngle);
        double t = off / span;
        if (t > kEps && t < 1.0 - kEps)
            ts.push_back(t);
    }
    std::sort(ts.begin(), ts.end());

    for (size_t i = 0; i + 1 < ts.size(); ++i) {
        double ta = ts[i], tb = ts[i + 1];
        if (tb - ta < kEps)
            continue;
        double midDeg = arc.startAngle + span * (0.5 * (ta + tb));
        double midRad = MyMath::deg2rad(midDeg);
        MyVector mid(arc.center.x + arc.radius * std::cos(midRad),
                     arc.center.y + arc.radius * std::sin(midRad));
        if (!insideBoundary(mid))
            continue;
        double s = arc.startAngle + span * ta;
        double sp = span * (tb - ta);
        QGraphicsItem *item = ctx->entityFactory()->createArc(
            mapPoint(arc.center), arc.radius * m_scale, s, sp, pen);
        if (item)
            out.append(item);
    }
}

void DetailViewTool::clipCircleToBoundary(const MyCircle &circle,
                                          const QPen &pen,
                                          QList<QGraphicsItem *> &out) const {
    IAppContext *ctx = getContext();
    double dx = m_center.x() - circle.center.x;
    double dy = m_center.y() - circle.center.y;
    double dc = std::sqrt(dx * dx + dy * dy);

    // 원 전체가 경계 안 → 원 그대로
    if (dc + circle.radius <= m_radius + kEps) {
        QGraphicsItem *item = ctx->entityFactory()->createCircle(
            mapPoint(circle.center), circle.radius * m_scale, pen);
        if (item)
            out.append(item);
        return;
    }
    // 경계와 안 만남 (원이 완전히 밖, 또는 경계를 통째로 감쌈) → 남는 호 없음
    if (dc - circle.radius >= m_radius - kEps)
        return;

    MyCircle boundary(MyVector(m_center), m_radius);
    std::vector<MyVector> hits =
        MyMath::intersectCircleCircle(boundary, circle);
    if (hits.size() < 2)
        return;

    double a0 = MyMath::normalizeAngle(MyMath::rad2deg(
        std::atan2(hits[0].y - circle.center.y, hits[0].x - circle.center.x)));
    double a1 = MyMath::normalizeAngle(MyMath::rad2deg(
        std::atan2(hits[1].y - circle.center.y, hits[1].x - circle.center.x)));
    double span01 = MyMath::normalizeAngle(a1 - a0);
    if (span01 < kEps)
        span01 = 360.0;

    double midRad = MyMath::deg2rad(a0 + span01 * 0.5);
    MyVector mid(circle.center.x + circle.radius * std::cos(midRad),
                 circle.center.y + circle.radius * std::sin(midRad));

    double s, sp;
    if (insideBoundary(mid)) {
        s = a0;
        sp = span01;
    } else {
        s = a1;
        sp = 360.0 - span01;
    }
    QGraphicsItem *item = ctx->entityFactory()->createArc(
        mapPoint(circle.center), circle.radius * m_scale, s, sp, pen);
    if (item)
        out.append(item);
}

void DetailViewTool::clipPolylineToBoundary(const MyPolyline &pline,
                                            const QPen &pen,
                                            QList<QGraphicsItem *> &out) const {
    clipPointRunToBoundary(pline.vertices, pline.isClosed, pen, out);
}

void DetailViewTool::clipPointRunToBoundary(const std::vector<MyVector> &pts,
                                            bool closed, const QPen &pen,
                                            QList<QGraphicsItem *> &out) const {
    IAppContext *ctx = getContext();
    const size_t vn = pts.size();
    if (vn < 2)
        return;
    const size_t segN = closed ? vn : vn - 1;

    MyCircle boundary(MyVector(m_center), m_radius);

    // 세그먼트별로 파라미터 u(=세그먼트 인덱스 + 국소 t)에서 잘라 안쪽 조각만 모은다
    std::vector<MyVector> run;
    for (size_t i = 0; i < segN; ++i) {
        const MyVector &a = pts[i];
        const MyVector &b = pts[(i + 1) % vn];
        MyVector d = b - a;
        double len2 = d.x * d.x + d.y * d.y;

        std::vector<double> ts;
        ts.push_back(0.0);
        ts.push_back(1.0);
        if (len2 >= kEps) {
            MyLine seg(a, b);
            std::vector<MyVector> hits =
                MyMath::intersectLineCircle(seg, boundary);
            for (const MyVector &h : hits) {
                double t = ((h.x - a.x) * d.x + (h.y - a.y) * d.y) / len2;
                if (t > kEps && t < 1.0 - kEps)
                    ts.push_back(t);
            }
        }
        std::sort(ts.begin(), ts.end());

        for (size_t k = 0; k + 1 < ts.size(); ++k) {
            double ta = ts[k], tb = ts[k + 1];
            if (tb - ta < kEps)
                continue;
            MyVector mid = a + d * (0.5 * (ta + tb));
            MyVector pa = a + d * ta;
            MyVector pb = a + d * tb;
            if (insideBoundary(mid)) {
                if (run.empty() || run.back().distanceTo(pa) > 1e-4)
                    run.push_back(pa);
                run.push_back(pb);
            } else {
                // 바깥 구간을 만나면 현재 run 을 끊어서 배출
                if (run.size() >= 2) {
                    std::vector<MyVector> mapped;
                    mapped.reserve(run.size());
                    for (const MyVector &p : run)
                        mapped.push_back(MyVector(mapPoint(p)));
                    QGraphicsItem *item = ctx->entityFactory()->createPolyline(
                        mapped, false, pen);
                    if (item)
                        out.append(item);
                }
                run.clear();
            }
        }
    }
    if (run.size() >= 2) {
        std::vector<MyVector> mapped;
        mapped.reserve(run.size());
        for (const MyVector &p : run)
            mapped.push_back(MyVector(mapPoint(p)));
        QGraphicsItem *item =
            ctx->entityFactory()->createPolyline(mapped, false, pen);
        if (item)
            out.append(item);
    }
}

void DetailViewTool::cloneAtomicIfInside(CadItem *src,
                                         QList<QGraphicsItem *> &out) const {
    if (!src)
        return;
    QPointF ctr = src->sceneBoundingRect().center();
    if (!insideBoundary(MyVector(ctr)))
        return;

    CadItem *c = src->clone();
    if (!c)
        return;
    c->move(MyVector(-m_center.x(), -m_center.y()));
    c->scale(MyVector(0.0, 0.0), m_scale);
    c->move(MyVector(m_place.x(), m_place.y()));
    out.append(c);
}

void DetailViewTool::emitDimensionIfContained(
    CadItem *src, QList<QGraphicsItem *> &out) const {
    if (!src)
        return;

    // 경계원에 "완전히" 들어와야만 재생성 (걸치거나 벗어나면 무시)
    QPainterPath circlePath;
    circlePath.addEllipse(m_center, m_radius, m_radius);

    QPainterPath geom = src->mapToScene(src->geometryPath());
    bool contained;
    if (!geom.isEmpty()) {
        contained = circlePath.contains(geom);
    } else {
        QPainterPath bp;
        bp.addPolygon(src->mapToScene(src->boundingRect()));
        contained = circlePath.contains(bp);
    }
    if (!contained)
        return;

    CadItem *c = src->clone();
    if (!c)
        return;

    // 위치·크기: 상세도 좌표계로 이동·확대
    c->move(MyVector(-m_center.x(), -m_center.y()));
    c->scale(MyVector(0.0, 0.0), m_scale);
    c->move(MyVector(m_place.x(), m_place.y()));

    // 표시 숫자 고정: 각도 치수는 스케일 불변이라 그대로 두고,
    // 그 외에는 measureScale = (원본 measureScale) / 상세도축척 으로 확대 배율을 상쇄한다.
    // (수동 텍스트가 아니라 실측 배율이라 이후 그립 편집·공차 계산에도 일관되게 반영됨)
    const MyEntity *e = src->getEntityData();
    const MyDimension *dim =
        e ? static_cast<const MyDimension *>(e) : nullptr;
    bool angular = dim && dim->dimensionType == DimensionType::Angular;
    if (!angular && m_scale > 0.0) {
        double srcMs = (dim && dim->measureScale > 0.0) ? dim->measureScale : 1.0;
        c->setProperty(QStringLiteral("MeasureScale"), srcMs / m_scale);
    }

    out.append(c);
}

// ──────────────────────────────────────────────────────────────
// 라벨 / 레이어 헬퍼
// ──────────────────────────────────────────────────────────────
QString DetailViewTool::nextDetailLabel() const {
    QSet<QString> used;
    if (m_scene) {
        const QList<QGraphicsItem *> all = m_scene->items();
        for (QGraphicsItem *it : all) {
            // 상세도 경계원(및 경계선 마커)에 태깅된 라벨을 수집 — 레이어명과 무관
            const QVariant lv = it->data(DETAIL_REGION_LABEL);
            if (lv.isValid() && !lv.toString().isEmpty()) {
                used.insert(lv.toString().toUpper());
                continue;
            }
            CadItem *ci = dynamic_cast<CadItem *>(it);
            if (ci) {
                const QString n = ci->layerName();
                if (n.startsWith(QLatin1String("DETAIL-")) && n.size() == 8)
                    used.insert(n.mid(7, 1).toUpper());
            }
        }
    }
    for (char c = 'A'; c <= 'Z'; ++c) {
        QString s(QChar::fromLatin1(c));
        if (!used.contains(s))
            return s;
    }
    return QStringLiteral("A");
}

// ── "2:1" / "1:2" / "2" 등 → 배율 ────────────────────────────────────────
double DetailViewTool::parseScaleRatio(const QString &text, double fallback) {
    QString t = text.trimmed();
    if (t.isEmpty())
        return fallback;
    if (t.contains(QLatin1Char(':'))) {
        const QStringList parts = t.split(QLatin1Char(':'));
        if (parts.size() == 2) {
            bool okA = false, okB = false;
            double a = parts.at(0).trimmed().toDouble(&okA);
            double b = parts.at(1).trimmed().toDouble(&okB);
            if (okA && okB && qAbs(b) > kEps && a > 0.0)
                return a / b;
        }
        return fallback;
    }
    bool ok = false;
    double v = t.toDouble(&ok);
    return (ok && v > kEps) ? v : fallback;
}

// 3-2: 도면 스코프 우선(ToolDefaults). 네임스페이스 "DetailViewTool" = 기존 그룹명.
//   세션당 1회 가드(s_settingsLoaded)를 없애고 매 activate()마다 현재 도면값을 읽는다.
void DetailViewTool::loadSettings(MyGraphicsScene *scene) {
    ToolDefaults d("DetailViewTool", scene);
    s_detailLayer     = d.getString("DetailLayer",     QString());
    s_labelTextSize   = d.getDouble("LabelTextSize",   0.0);
    s_labelOnLeader   = d.getBool("LabelOnLeader",   true);
    s_labelOnBoundary = d.getBool("LabelOnBoundary", false);
    s_scaleText       = d.getString("ScaleText", QStringLiteral("2:1"));
    // 라디오(택1)로 정규화: "경계선"이 명확히 선택된 경우만 유지, 그 외엔 "지시선"
    const bool useBoundary = s_labelOnBoundary && !s_labelOnLeader;
    s_labelOnBoundary = useBoundary;
    s_labelOnLeader = !useBoundary;
}

void DetailViewTool::saveSettings(MyGraphicsScene *scene) {
    ToolDefaults d("DetailViewTool", scene);
    d.put("DetailLayer",     s_detailLayer);
    d.put("LabelTextSize",   s_labelTextSize);
    d.put("LabelOnLeader",   s_labelOnLeader);
    d.put("LabelOnBoundary", s_labelOnBoundary);
    d.put("ScaleText",       s_scaleText);
}

void DetailViewTool::openSetupDialog() {
    IAppContext *ctx = getContext();
    if (!ctx)
        return;

    QList<QPair<QString, QColor>> layers;
    if (ctx->layerManager()) {
        const QList<Layer> all = ctx->layerManager()->getAllLayers();
        for (int i = 0; i < all.size(); ++i)
            layers.append(qMakePair(all.at(i).name(), all.at(i).color()));
    }
    QString curLayer = s_detailLayer;
    if (curLayer.isEmpty())
        curLayer = currentLayerName();

    QWidget *parentWin =
        ctx->commandDock() ? dynamic_cast<QWidget *>(ctx->commandDock()) : nullptr;

    DetailViewSetupDialog dlg(layers, curLayer, s_labelTextSize, s_labelOnLeader,
                              s_labelOnBoundary, s_scaleText, parentWin);
    if (dlg.exec() == QDialog::Accepted) {
        s_detailLayer     = dlg.layerName();
        s_labelTextSize   = dlg.labelTextSize();
        s_labelOnLeader   = dlg.labelOnLeader();
        s_labelOnBoundary = dlg.labelOnBoundary();
        s_scaleText       = dlg.scaleText();
        m_scale = parseScaleRatio(s_scaleText, m_scale);
        saveSettings(m_scene);
    }
    showCenterPrompt();
}

void DetailViewTool::ensureDetailLayer(const QString &layerName) {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->layerManager())
        return;
    if (ctx->layerManager()->getLayer(layerName))
        return;
    Layer layer(layerName, QColor(255, 255, 255), 0.25,
                QStringLiteral("Continuous"), false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

QString DetailViewTool::formatScaleRatio(double factor) {
    if (factor <= kEps)
        return QStringLiteral("1 : 1");
    if (factor >= 1.0)
        return QString::fromLatin1("%1 : 1").arg(trimTrailingZeros(factor));
    return QString::fromLatin1("1 : %1").arg(trimTrailingZeros(1.0 / factor));
}

// ──────────────────────────────────────────────────────────────
// 상세도 생성 본체
// ──────────────────────────────────────────────────────────────
void DetailViewTool::buildDetailView(const QPointF &placePos) {
    IAppContext *ctx = getContext();
    if (!m_scene || !ctx || !ctx->entityFactory()) {
        cancelDrawing();
        emit toolFinished();
        return;
    }

    m_place = placePos;

    QList<CadItem *> sources = collectItemsInCircle(m_center, m_radius);
    if (sources.isEmpty()) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("경계 안에 객체가 없습니다. 상세도 생성을 취소합니다."),
                CommandDock::Warning);
        cancelDrawing();
        emit toolFinished();
        return;
    }

    EntityFactory *ef = ctx->entityFactory();
    const QString label = nextDetailLabel();
    // Setup에서 레이어를 지정했으면 그것을, 아니면 DETAIL-<라벨> 자동 레이어를 사용
    QString detailLayer = s_detailLayer.trimmed();
    if (detailLayer.isEmpty())
        detailLayer = QString::fromLatin1("DETAIL-%1").arg(label);
    ensureDetailLayer(detailLayer);

    // 1) 경계 안쪽만 클리핑한 개별 엔티티 생성.
    //    각 조각은 원본 펜 + **원본 객체의 레이어**를 그대로 물려받는다.
    //    (상세도 전용 레이어로 강제하면 저장→재오픈 시 ByLayer 색상이 그 레이어
    //     색으로 바뀌어 "레이어가 변경된 것처럼" 보이는 문제가 생긴다.)
    QList<QGraphicsItem *> created;
    for (CadItem *src : sources) {
        if (!src)
            continue;
        const int before = created.size();
        emitClippedItem(src, src->pen(), created);
        const QString srcLayer = src->layerName();
        if (!srcLayer.isEmpty()) {
            for (int k = before; k < created.size(); ++k) {
                if (CadItem *ci = dynamic_cast<CadItem *>(created.at(k)))
                    ci->setLayerName(srcLayer);
            }
        }
    }
    if (created.isEmpty()) {
        if (ctx->commandDock())
            ctx->commandDock()->logMessage(
                tr("경계 안쪽에 남는 형상이 없습니다. 상세도 생성을 취소합니다."),
                CommandDock::Warning);
        cancelDrawing();
        emit toolFinished();
        return;
    }

    QPen layerPen = getLayerPen(detailLayer);

    // 2) 상세도 경계원 — 이 원 안에서 새로 그리는 치수가 축척을 상쇄하도록 태깅
    QPen boundaryPen = layerPen;
    boundaryPen.setStyle(Qt::SolidLine);
    if (QGraphicsItem *b =
            ef->createCircle(m_place, m_radius * m_scale, boundaryPen)) {
        b->setData(DETAIL_REGION_LABEL, label);
        b->setData(DETAIL_REGION_SCALE, m_scale);
        created.append(b);
    }

    // 3) 캡션 — 도면 축척 기준 글자 크기로 고정(배율 영향 배제)
    double drawScale = m_scene->drawingScale();
    if (drawScale <= kEps)
        drawScale = 1.0;
    double capSize = ctx->currentFontSize() * drawScale;
    if (capSize <= kEps)
        capSize = 2.5 * drawScale;

    QFont capFont;
    capFont.setFamily(ctx->currentFontFamily());
    capFont.setPointSizeF(capSize);
    QColor capColor = m_scene->getLayerStyle(detailLayer).color;

    double capGap = capSize * 1.4;
    double baseY = m_place.y() - (m_radius * m_scale + capGap);
    QString line1 = tr("상세도  %1").arg(label);
    QString line2 = tr("축척  %1").arg(formatScaleRatio(m_scale));
    if (QGraphicsItem *t1 = ef->createText(line1, QPointF(m_place.x(), baseY),
                                           capFont, capColor, 0.0, 1))
        created.append(t1);
    if (QGraphicsItem *t2 = ef->createText(line2,
                                           QPointF(m_place.x(), baseY - capGap),
                                           capFont, capColor, 0.0, 1)) {
        t2->setData(DETAIL_REGION_SCALE_CAPTION, 1); // 축척값 변경 시 자동 갱신 대상
        created.append(t2);
    }

    // 4) 원본 위치의 참조 마커 (경계선/지시선 + 라벨) — Setup 설정에 따라
    double labelSize = (s_labelTextSize > 0.0 ? s_labelTextSize
                                              : ctx->currentFontSize()) *
                       drawScale;
    if (labelSize <= kEps)
        labelSize = capSize;
    QList<QGraphicsItem *> markers;
    buildSourceMarker(label, labelSize, detailLayer, markers);

    // 5) Undo 매크로로 묶어 push
    QUndoStack *stack = ctx->undoStack();
    if (stack)
        stack->beginMacro(tr("상세도 %1 생성").arg(label));

    for (int i = 0; i < created.size(); ++i) {
        QGraphicsItem *item = created.at(i);
        // 클리핑 조각은 위에서 원본 레이어를 물려받았다. 레이어가 비어있는
        // 아이템(상세도 경계원·캡션 등 주석 요소)만 상세도 레이어로 보낸다.
        if (CadItem *ci = dynamic_cast<CadItem *>(item)) {
            if (ci->layerName().isEmpty())
                ci->setLayerName(detailLayer);
        }
        item->setData(DETAIL_REGION_ROLE, DETAIL_ROLE_CONTENT);
        item->setData(DETAIL_REGION_LABEL, label); // 어느 상세도 소속인지 표식
        if (stack)
            stack->push(new AddEntityCommand(m_scene, item));
        else
            m_scene->addItem(item);
    }
    for (int i = 0; i < markers.size(); ++i) {
        if (stack)
            stack->push(new AddEntityCommand(m_scene, markers.at(i)));
        else
            m_scene->addItem(markers.at(i));
    }

    if (stack)
        stack->endMacro();

    if (ctx->commandDock()) {
        ctx->commandDock()->logMessage(
            tr("상세도 %1 생성 완료 (원본 %2개 → 조각 %3개, 축척 %4).")
                .arg(label)
                .arg(sources.size())
                .arg(created.size())
                .arg(formatScaleRatio(m_scale)),
            CommandDock::Info);
        ctx->commandDock()->setPrompt(QString::fromUtf8(" Command : "));
    }

    cancelDrawing();
    emit toolFinished();
}

// ──────────────────────────────────────────────────────────────
// 원본 위치 참조 마커 (지시선 / 경계선 모드)
// ──────────────────────────────────────────────────────────────
void DetailViewTool::buildSourceMarker(const QString &label, double labelSize,
                                       const QString &layerName,
                                       QList<QGraphicsItem *> &out) const {
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->entityFactory() || !m_scene)
        return;
    EntityFactory *ef = ctx->entityFactory();

    // 원본 위치 마커(경계원 + 라벨)는 Setup에서 설정된(또는 자동) 상세도 레이어에 둔다
    QPen markerPen = getLayerPen(layerName);
    markerPen.setStyle(Qt::SolidLine);
    QColor labelColor = m_scene->getLayerStyle(layerName).color;

    QFont labelFont;
    labelFont.setFamily(ctx->currentFontFamily());
    labelFont.setPointSizeF(labelSize > 0.0 ? labelSize : 2.5);

    const double angleDeg = 45.0; // 라벨 방향(우상향)
    const double angleRad = MyMath::deg2rad(angleDeg);
    const MyVector dir(std::cos(angleRad), std::sin(angleRad));
    const MyVector c(m_center);
    const MyVector onCircle = c + dir * m_radius; // 경계원 위의 라벨 기준점

    // ── 경계선 모드: 경계원을 라벨 자리만큼 끊고 그 자리에 라벨 ──
    if (s_labelOnBoundary) {
        double approxTextW = labelSize * 0.75 * qMax(1, label.size());
        double gapArc = approxTextW * 1.8;
        double gapHalfDeg =
            MyMath::rad2deg((gapArc * 0.5) / qMax(m_radius, kEps));
        gapHalfDeg = qBound(4.0, gapHalfDeg, 45.0);

        double arcStart = angleDeg + gapHalfDeg;
        double arcSpan = 360.0 - 2.0 * gapHalfDeg;
        if (QGraphicsItem *arc = ef->createArc(m_center, m_radius, arcStart,
                                               arcSpan, markerPen)) {
            if (CadItem *ac = dynamic_cast<CadItem *>(arc))
                ac->setLayerName(layerName);
            out.append(arc);
        }
        if (QGraphicsItem *tx = ef->createText(label, onCircle.toPointF(),
                                               labelFont, labelColor, 0.0, 1)) {
            if (CadItem *tc = dynamic_cast<CadItem *>(tx))
                tc->setLayerName(layerName);
            out.append(tx);
        }
    }

    // ── 지시선 모드: 완전한 경계원 + 지시선(화살표) + 라벨 텍스트 ──
    if (s_labelOnLeader || !s_labelOnBoundary) {
        if (QGraphicsItem *circle =
                ef->createCircle(m_center, m_radius, markerPen)) {
            if (CadItem *cc = dynamic_cast<CadItem *>(circle))
                cc->setLayerName(layerName);
            out.append(circle);
        }
        double leaderLen = (2.0 * m_radius) / 3.0; // 지시선 길이 ≈ 원 지름의 1/3
        MyVector elbow = c + dir * (m_radius + leaderLen);
        MyLeader ld;
        ld.points << onCircle << elbow;
        ld.landingLength = labelSize * 2.2; // 라벨이 가운데 놓일 만큼 착지선 확보
        if (QGraphicsItem *leader = ef->createLeader(ld, markerPen)) {
            if (CadItem *lc = dynamic_cast<CadItem *>(leader))
                lc->setLayerName(layerName);
            out.append(leader);
        }
        // 라벨은 착지선(꺾인 선)의 "중앙"에 놓는다. LeaderItem 이 그리는 착지선은
        //   elbow → elbow + (+x, 0), 길이 = drawingScale * max(landingLength, 2.0)
        double ds = m_scene ? m_scene->drawingScale() : 1.0;
        if (ds <= 0.0)
            ds = 1.0;
        double landingW = ds * qMax(ld.landingLength, 2.0);
        MyVector labelPos =
            elbow + MyVector(landingW * 0.5, labelSize * 0.4);
        if (QGraphicsItem *tx = ef->createText(label, labelPos.toPointF(),
                                               labelFont, labelColor, 0.0, 1)) {
            if (CadItem *tc = dynamic_cast<CadItem *>(tx))
                tc->setLayerName(layerName);
            out.append(tx);
        }
    }

    // 이 함수가 만든 마커 아이템은 "원본 참조마커"로 태깅 (상세도 내용과 구분)
    for (int i = 0; i < out.size(); ++i)
        out.at(i)->setData(DETAIL_REGION_ROLE, DETAIL_ROLE_MARKER);
}

void DetailViewTool::cancelDrawing() {
    m_state = WaitCenter;
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
}

void DetailViewTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        cancelDrawing();
        emit toolFinished();
    } else {
        AbstractTool::keyPress(event);
    }
}
