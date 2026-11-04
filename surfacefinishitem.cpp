#include "surfacefinishitem.h"
#include "mymath.h"
#include "mygraphicsscene.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "layer.h"
#include "sceneprinter.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QPainterPathStroker>
#include <QFontMetricsF>
#include <QtMath>

SurfaceFinishItem::SurfaceFinishItem(const SurfaceFinishData &data, QGraphicsItem *parent)
    : CadItem(parent), m_data(data)
{
    setZValue(1);
}

double SurfaceFinishItem::getScale() const {
    if (m_overrideScale > 0) return m_overrideScale;
    if (MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene())) {
        return sc->drawingScale();
    }
    return 1.0;
}

double SurfaceFinishItem::effectiveArrowSize() const {
    if (m_overrideArrowSize >= 0.0) return m_overrideArrowSize;
    if (MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene())) {
        return sc->dimensionStyle().arrowSize;
    }
    return -1.0;
}

double SurfaceFinishItem::unitSize() const {
    double base = m_data.textHeight;
    double arrowSize = effectiveArrowSize();
    if (arrowSize > 0.0) {
        // 지시선 화살표와 비슷한 눈에 띄는 크기를 보장 (문자 높이가 아주 작아도
        // 체크마크가 너무 작아져 안 보이는 문제 방지). 씬에 추가되기 전
        // 고스트 미리보기 단계에서는 m_overrideArrowSize로 동일하게 주입됨.
        base = qMax(base, arrowSize * 1.6);
    }
    double result = base * getScale();

    // 지시선을 길게 그린 경우, 체크마크가 지시선에 비해 상대적으로 너무
    // 작아서 안 보이는 일이 없도록 지시선 총 길이에 비례한 최소 크기도 보장.
    // 단, 이 비율이 너무 크면 기호(및 선택 판정 영역)가 과도하게 커지므로
    // 완만하게만 키운다.
    if (m_data.leaderPoints.size() >= 2) {
        double totalLen = 0.0;
        for (int i = 1; i < m_data.leaderPoints.size(); ++i) {
            totalLen += m_data.leaderPoints[i].distanceTo(m_data.leaderPoints[i - 1]);
        }
        result = qMax(result, totalLen * 0.08);
    }
    return result;
}

double SurfaceFinishItem::getDrawRotation() const {
    double ang = MyMath::normalizeAngle(m_data.rotation);
    if (ang > 90.0 && ang < 270.0) {
        return MyMath::normalizeAngle(m_data.rotation + 180.0);
    }
    return m_data.rotation;
}

bool SurfaceFinishItem::isFlipped() const {
    double ang = MyMath::normalizeAngle(m_data.rotation);
    return (ang > 90.0 && ang < 270.0);
}

QPointF SurfaceFinishItem::vertexPoint() const {
    return QPointF(0.0, 0.0);
}

QPointF SurfaceFinishItem::shortLegPoint() const {
    double u = unitSize();
    double fs = isFlipped() ? -1.0 : 1.0;
    // 꼭짓점 기준 왼쪽 위로 짧게 (longLeg와 반대 방향이어야 "√" 모양이 됨)
    // 씬 좌표는 Y가 위로 증가하는 CAD 관례(MyGraphicsView::setupBottomLeftOrigin)를
    // 따르므로, "위"는 +y 이다.
    return QPointF(-0.45 * u * fs, 0.75 * u);
}

QPointF SurfaceFinishItem::longLegPoint() const {
    double u = unitSize();
    double fs = isFlipped() ? -1.0 : 1.0;
    // 꼭짓점 기준 오른쪽 위로 길게
    return QPointF(1.3 * u * fs, 2.0 * u);
}

QPointF SurfaceFinishItem::barEndPoint() const {
    double u = unitSize();
    double fs = isFlipped() ? -1.0 : 1.0;
    QPointF lp = longLegPoint();

    // 막대(꼬리선)는 그 아래에 표시될 Ra 값 문자열의 실제 폭에 맞춰
    // 최소 길이를 넉넉히 늘린다 (기본 길이보다 짧아지지는 않음).
    double barLen = 1.3 * u;
    QString text = displayLine1();
    if (!text.isEmpty()) {
        QFont f;
        f.setPixelSize(qMax(1, qRound(u)));
        QFontMetricsF fm(f);
        double textW = fm.horizontalAdvance(text);
        barLen = qMax(barLen, textW + 0.4 * u);
    }
    return QPointF(lp.x() + barLen * fs, lp.y());
}

QString SurfaceFinishItem::displayLine1() const {
    // 구기호(▽)는 기호만 표시하고 등급명 문자는 표기하지 않는다.
    if (m_data.symbol == SurfaceFinishSymbol::LegacyGrade) return QString();
    // W/X/Y/Z 약식 표기: 숫자 값 대신 글자 하나만 표시
    if (!m_data.raLetterCode.isEmpty()) return m_data.raLetterCode;
    if (!m_data.raValue.isEmpty()) return QStringLiteral("Ra ") + m_data.raValue;
    return QString();
}

QPointF SurfaceFinishItem::circleCenter() const {
    // p1-p2(짧은 다리)와 p2-p3(긴 다리) 두 변에 모두 접하는(법선 거리 = 반지름)
    // 내접원의 중심: 꼭짓점(p2, 원점)에서 두 변의 각 이등분선 방향으로
    // r / sin(끼인각의 절반) 만큼 떨어진 지점.
    double r = circleRadius();
    QPointF a = shortLegPoint(); // p2 -> p1 방향
    QPointF b = longLegPoint();  // p2 -> p3 방향
    double la = qSqrt(a.x() * a.x() + a.y() * a.y());
    double lb = qSqrt(b.x() * b.x() + b.y() * b.y());
    if (la < 1e-6 || lb < 1e-6) return QPointF(0.0, r);

    QPointF ua(a.x() / la, a.y() / la);
    QPointF ub(b.x() / lb, b.y() / lb);
    QPointF bis(ua.x() + ub.x(), ua.y() + ub.y());
    double lbis = qSqrt(bis.x() * bis.x() + bis.y() * bis.y());
    if (lbis < 1e-6) return QPointF(0.0, r);

    QPointF ubis(bis.x() / lbis, bis.y() / lbis);
    double cosHalf = ua.x() * ubis.x() + ua.y() * ubis.y();
    double sinHalf = qSqrt(qMax(0.0, 1.0 - cosHalf * cosHalf));
    double d = (sinHalf > 1e-4) ? (r / sinHalf) : (r * 3.0);
    return QPointF(ubis.x() * d, ubis.y() * d);
}

double SurfaceFinishItem::circleRadius() const {
    // 다리를 크게 덮지 않도록 짧은 다리 길이 기준으로 아담하게
    return 0.32 * unitSize();
}

QPointF SurfaceFinishItem::triangleClosePoint() const {
    // vertex→longLegPoint(긴 다리) 선 "위"에서, 짧은 다리 끝(p1)과 같은 높이인
    // 점을 고른다. 이렇게 하면 삼각형의 p4-vertex 변이 긴 다리 선과 완전히
    // 겹치므로, 기본형(√)과 겹쳐 봤을 때 긴 다리가 두 개의 선으로 어긋나
    // 보이지 않는다. (닫힌 삼각형이 정삼각형일 필요는 없음)
    QPointF sl = shortLegPoint();
    QPointF ll = longLegPoint();
    if (qAbs(ll.y()) < 1e-6) return QPointF(0.0, sl.y());
    double t = sl.y() / ll.y();
    return QPointF(ll.x() * t, sl.y());
}

QPointF SurfaceFinishItem::textAnchorPoint() const {
    double u = unitSize();
    double fs = isFlipped() ? -1.0 : 1.0;

    if (m_data.symbol == SurfaceFinishSymbol::LegacyGrade) {
        int count = qBound(1, m_data.legacyGrade, 4);
        double th = 0.5 * u, gap = 0.18 * u;
        double stackTop = 0.25 * u + count * (th + gap);
        return QPointF(0.75 * u * fs, (stackTop + 0.25 * u) * 0.5);
    }

    if (m_data.showTailBar) {
        // 꼬리선(막대) 아래쪽에 값 텍스트를 배치
        QPointF ll = longLegPoint();
        QPointF be = barEndPoint();
        return QPointF(ll.x(), be.y() - 1.0 * u);
    }
    QPointF lp = longLegPoint();
    return QPointF(lp.x() + 0.2 * u * fs, lp.y() + 0.15 * u);
}

QTransform SurfaceFinishItem::frameTransform() const {
    QTransform t;
    t.translate(m_data.position.x, m_data.position.y);
    t.rotate(getDrawRotation());
    return t;
}

QList<MyVector> SurfaceFinishItem::getAdjustedLeaderPoints() const {
    // 체크마크는 GDT 프레임과 달리 사각 박스가 없으므로, 지시선은
    // 꼭짓점(position)까지 그대로 연결하면 됨 (경계 교차 보정 불필요)
    return m_data.leaderPoints;
}

QPainterPath SurfaceFinishItem::localGlyphPath() const {
    QPainterPath path;

    if (m_data.symbol == SurfaceFinishSymbol::LegacyGrade) {
        double u = unitSize();
        double fs = isFlipped() ? -1.0 : 1.0;
        double tw = 1.0 * u, th = 0.5 * u, gap = 0.25 * u;
        int count = qBound(1, m_data.legacyGrade, 4);
        double pointY = 0.25 * u; // 뾰족한 아래쪽 끝 (모든 삼각형 동일 높이)
        double flatY = pointY + th; // 평평한 윗변 (▽ 모양)

        // 세로로 쌓지 않고 한 줄로 가로 배열 (그룹 전체를 가운데 정렬)
        double totalWidth = count * tw + (count - 1) * gap;
        for (int i = 0; i < count; ++i) {
            double localCx = -totalWidth * 0.5 + i * (tw + gap) + tw * 0.5;
            double cx = localCx * fs;
            QPolygonF tri;
            tri << QPointF(cx - 0.5 * tw, flatY)
                << QPointF(cx + 0.5 * tw, flatY)
                << QPointF(cx, pointY);
            path.addPolygon(tri);
            path.closeSubpath();
        }
        return path;
    }

    if (m_data.symbol == SurfaceFinishSymbol::TriangleFlag) {
        // 기본형(shortLeg-vertex-longLeg)에서 shortLeg(p1)-vertex(p2) 변은 그대로 두고,
        // 긴 다리 선 위의 점 p4(triangleClosePoint)를 추가해 p1-p4-p2 삼각형을 닫는다.
        // 긴 다리 자체는 기본형과 똑같이 vertex에서 그대로 그리므로(겹치는 선),
        // 기본형과 겹쳐 봤을 때 긴 다리가 어긋나 보이지 않는다.
        QPointF p1 = shortLegPoint();
        QPointF p4 = triangleClosePoint();
        QPointF p2 = vertexPoint();

        QPolygonF tri;
        tri << p1 << p4 << p2;
        path.addPolygon(tri);
        path.closeSubpath();

        path.moveTo(p2);
        path.lineTo(longLegPoint());
        if (m_data.showTailBar) {
            path.moveTo(longLegPoint());
            path.lineTo(barEndPoint());
        }
        return path;
    }

    path.moveTo(shortLegPoint());
    path.lineTo(vertexPoint());
    path.lineTo(longLegPoint());

    if (m_data.symbol == SurfaceFinishSymbol::MachiningProhibited) {
        path.addEllipse(circleCenter(), circleRadius(), circleRadius());
    }
    if (m_data.showTailBar) {
        path.moveTo(longLegPoint());
        path.lineTo(barEndPoint());
    }
    return path;
}

QRectF SurfaceFinishItem::boundingRect() const {
    return shape().boundingRect().adjusted(-2, -2, 2, 2);
}

QPainterPath SurfaceFinishItem::shape() const {
    QPainterPath p;
    QList<MyVector> adjPoints = getAdjustedLeaderPoints();
    if (adjPoints.size() >= 2 && m_data.useLeader) {
        QPolygonF poly;
        for (const MyVector &v : adjPoints) poly << v.toPointF();
        QPainterPath linePath;
        linePath.addPolygon(poly);
        QPainterPathStroker stroker;
        stroker.setWidth(2.0);
        p.addPath(stroker.createStroke(linePath));
    }

    // 선택 판정 여유폭은 지시선 길이에 비례해 커지는 unitSize()를 그대로 쓰지
    // 않고 작게 눌러서, 주변의 다른 객체를 클릭할 때 이 기호가 대신 선택되는
    // 일이 없도록 한다.
    QPainterPathStroker glyphStroker;
    glyphStroker.setWidth(qMax(unitSize() * 0.035, m_pen.widthF() + 0.3 * getScale()));
    QPainterPath glyph = glyphStroker.createStroke(localGlyphPath());

    // 텍스트가 실제로 표시될 때만 클릭/선택 판정 영역에 반영 (너무 넉넉하지
    // 않도록 실제 텍스트 크기에 가깝게)
    if (!displayLine1().isEmpty()) {
        QPointF ta = textAnchorPoint();
        double u = unitSize();
        QRectF textBox(ta.x(), ta.y() - u * 0.4, u * 1.8 * (isFlipped() ? -1.0 : 1.0), u * 0.8);
        glyph.addRect(textBox.normalized());
    }

    p.addPath(frameTransform().map(glyph));
    return p;
}

QPainterPath SurfaceFinishItem::geometryPath() const {
    QPainterPath path;
    QList<MyVector> adjPoints = getAdjustedLeaderPoints();
    if (adjPoints.size() >= 2 && m_data.useLeader) {
        QPolygonF poly;
        for (const MyVector &v : adjPoints) poly << v.toPointF();
        path.addPolygon(poly);
    }
    path.addPath(frameTransform().map(localGlyphPath()));
    return path;
}

void SurfaceFinishItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(widget);

    MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
    DimensionStyle style;
    if (sc) style = sc->dimensionStyle();

    bool showWeight = sc ? sc->showLineWeight() : false;
    if (painter->device()->devType() != QInternal::Widget) showWeight = true;

    // 기호(체크마크 등)와 값 문자에 각각 별도 레이어를 지정할 수 있다.
    // 지정된 레이어가 있으면 그 레이어의 색상·굵기를 그대로 적용하고,
    // 없으면 이 아이템 자신의 레이어(펜)를 그대로 사용한다.
    auto resolvePen = [&](const QString &layerOverride) -> QPen {
        QPen pen = m_pen;
        QString targetLayer = layerOverride.isEmpty() ? layerName() : layerOverride;
        if (sc && sc->context() && sc->context()->layerManager()) {
            LayerManager *lm = sc->context()->layerManager();
            if (Layer *layer = lm->getLayer(targetLayer)) {
                // [핵심] 인쇄 중일 때는 ScenePrinter와 동일한 출력 색상 규칙(레이어별 출력색
                // 오버라이드 우선, 없으면 밝은 색 자동 검정화)을 적용한다. 이 색은 매번 레이어
                // 매니저에서 새로 읽어오므로, ScenePrinter가 아이템의 pen()만 임시로 바꿔치기
                // 하는 방식으로는 영향을 줄 수 없었다 (그래서 흑백 인쇄 시 거칠기 기호만
                // 원래 밝은 색 그대로 나와 흐리게 보였다).
                pen.setColor(ScenePrinter::resolvePrintColorForPaint(painter, layer->color(), lm, targetLayer));
                if (showWeight) {
                    pen.setCosmetic(false);
                    pen.setWidthF(layer->weight());
                    qreal sc2 = qAbs(painter->transform().m11());
                    if (sc2 > 0.0 && pen.widthF() * sc2 < 1.0) pen.setWidthF(1.0 / sc2);
                    return pen;
                }
            }
        }
        pen.setCosmetic(true);
        pen.setWidthF(1.0);
        return pen;
    };

    QPen linePen = resolvePen(m_data.symbolLayer);
    QPen textPen = resolvePen(m_data.textLayer);

    double finalScale = getScale();

    // 지시선 + 화살표
    QList<MyVector> adjPoints = getAdjustedLeaderPoints();
    if (adjPoints.size() >= 2 && m_data.useLeader) {
        painter->save();
        painter->setPen(linePen);

        QPolygonF poly;
        for (const MyVector &v : adjPoints) poly << v.toPointF();
        painter->drawPolyline(poly);

        MyVector p1 = adjPoints[0];
        MyVector p2 = adjPoints[1];
        MyVector dir = p2 - p1;
        if (dir.magnitude() > 1e-6) {
            dir = dir.normalized();
            double arrowSize = style.arrowSize * finalScale;
            if (arrowSize < 1e-6) arrowSize = 2.5 * finalScale;

            MyVector n(-dir.y, dir.x);
            MyVector head = p1;
            MyVector back = p1 + dir * arrowSize;
            MyVector side1 = back + n * (arrowSize * 0.35);
            MyVector side2 = back - n * (arrowSize * 0.35);

            QPolygonF arrowPoly;
            arrowPoly << head.toPointF() << side1.toPointF() << side2.toPointF();
            painter->setBrush(linePen.color());
            painter->drawPolygon(arrowPoly);
        }
        painter->restore();
    }

    painter->save();
    painter->translate(m_data.position.x, m_data.position.y);
    painter->rotate(getDrawRotation());
    // 뷰의 Y축 반전 여부에 따라 좌우로 다시 뒤집는 보정은 하지 않는다.
    // shape()/geometryPath()(고스트 미리보기, 클릭 판정)는 이 보정을 알 수
    // 없으므로 여기서만 보정하면 고스트와 실제 배치 결과가 서로 반전되어
    // 보이는 불일치가 생긴다. 기호는 항상 rotation=0(정자세)이므로 이 보정
    // 자체가 불필요하다.

    painter->setPen(linePen);
    painter->setBrush(Qt::NoBrush); // 구기호(▽)도 채우지 않고 외곽선만 표시
    painter->drawPath(localGlyphPath());

    // 값 텍스트
    QString line1 = displayLine1();
    QString line2;
    if (m_data.symbol != SurfaceFinishSymbol::LegacyGrade && !m_data.productionMethod.isEmpty()) {
        line2 = m_data.productionMethod;
    }

    if (!line1.isEmpty() || !line2.isEmpty()) {
        QFont f = painter->font();
        QString family = m_data.fontName.isEmpty() ? style.fontFamily : m_data.fontName;
        if (family.toLower().endsWith(".ttf") || family.toLower().endsWith(".shx")) {
            family = family.left(family.lastIndexOf('.'));
        }
        if (!family.isEmpty()) {
            QStringList families;
            families << family;
            if (family.contains(QString::fromUtf8("돋움"))) families << "Dotum" << QString::fromUtf8("돋움") << QString::fromUtf8("돋움체");
            if (family.contains(QString::fromUtf8("굴림"))) families << "Gulim" << QString::fromUtf8("굴림") << QString::fromUtf8("굴림체");
            if (family.contains(QString::fromUtf8("맑은"))) families << "Malgun Gothic" << QString::fromUtf8("맑은 고딕");

            bool found = false;
            for (const QString &fam : families) {
                f.setFamily(fam);
                QFontInfo info(f);
                if (info.family().toLower().contains(fam.toLower()) ||
                    info.family().toLower().contains("dotum") ||
                    info.family().toLower().contains("gulim")) {
                    found = true;
                    break;
                }
            }
            if (!found) f.setFamily("Arial");
        }

        double th_px = unitSize();
        f.setPixelSize(qMax(1, qRound(th_px)));
        painter->setFont(f);
        painter->setPen(textPen);

        QPointF ta = textAnchorPoint();
        bool flip = isFlipped();
        Qt::Alignment halign = flip ? Qt::AlignRight : Qt::AlignLeft;
        double boxW = th_px * 6.0;

        // drawText()는 Y가 아래로 증가한다고 가정하고 글자를 그리지만, 이 씬은
        // Y가 위로 증가하는 CAD 좌표계(MyGraphicsView::setupBottomLeftOrigin)이다.
        // 도형(체크마크)은 이미 올바르게 그려졌으니, 글자만 별도로
        // translate+scale(1,-1)로 국소적으로 보정해서 뒤집히지 않게 한다.
        painter->save();
        painter->translate(ta);
        painter->scale(1, -1);

        QRectF box1(flip ? -boxW : 0.0, -th_px * 0.575, boxW, th_px * 1.15);
        painter->drawText(box1, halign | Qt::AlignVCenter, line1);

        if (!line2.isEmpty()) {
            QFont f2 = f;
            f2.setPixelSize(qMax(1, qRound(th_px * 0.8)));
            painter->setFont(f2);
            QRectF box2(box1.left(), box1.bottom(), box1.width(), th_px);
            painter->drawText(box2, halign | Qt::AlignVCenter, line2);
        }
        painter->restore();
    }

    painter->restore();

    if (option && (option->state & QStyle::State_Selected) &&
        painter->device()->devType() == QInternal::Widget) {
        if (!(parentItem() && dynamic_cast<CadItem *>(parentItem()))) {
            painter->save();
            QPen glowPen(QColor(0, 255, 255, 80));
            glowPen.setCosmetic(true);
            glowPen.setWidth(5);
            painter->setPen(glowPen);
            painter->drawPath(geometryPath());

            QPen dashPen(Qt::white);
            dashPen.setCosmetic(true);
            dashPen.setWidth(1);
            dashPen.setStyle(Qt::DashLine);
            painter->setPen(dashPen);
            painter->drawPath(geometryPath());
            painter->restore();
        }
    }
}

CadItem *SurfaceFinishItem::clone() const {
    auto *item = new SurfaceFinishItem(m_data);
    item->setLayerName(layerName());
    item->setPen(m_pen);
    return item;
}

QList<SnapPoint> SurfaceFinishItem::getSnapPoints() const {
    QList<SnapPoint> snaps;
    for (const MyVector &v : m_data.leaderPoints) snaps << SnapPoint{v, SnapType::Endpoint};
    if (m_data.leaderPoints.isEmpty() || !m_data.useLeader) {
        snaps << SnapPoint{m_data.position, SnapType::Endpoint};
    }

    // 체크마크/삼각형/막대 등 실제로 그려지는 기호 외곽선의 끝점들도
    // 오브젝트 스냅(끝점) 후보로 제공한다.
    QTransform t = frameTransform();
    auto addLocal = [&](const QPointF &p) {
        snaps << SnapPoint{MyVector(t.map(p)), SnapType::Endpoint};
    };

    if (m_data.symbol == SurfaceFinishSymbol::LegacyGrade) {
        // 구기호는 지시선 끝점 스냅만으로 충분 (개별 삼각형 꼭짓점은 생략)
    } else if (m_data.symbol == SurfaceFinishSymbol::TriangleFlag) {
        addLocal(shortLegPoint());
        addLocal(triangleClosePoint());
        addLocal(vertexPoint());
        addLocal(longLegPoint());
    } else {
        addLocal(shortLegPoint());
        addLocal(vertexPoint());
        addLocal(longLegPoint());
        if (m_data.symbol == SurfaceFinishSymbol::MachiningProhibited) {
            addLocal(circleCenter());
        }
    }
    if (m_data.showTailBar && m_data.symbol != SurfaceFinishSymbol::LegacyGrade) {
        addLocal(barEndPoint());
    }
    return snaps;
}

QList<QPointF> SurfaceFinishItem::getGripPoints() const {
    QList<QPointF> grips;
    if (m_data.useLeader) {
        for (const MyVector &v : m_data.leaderPoints) grips << v.toPointF();
    }
    if (m_data.leaderPoints.isEmpty() || !m_data.useLeader) grips << m_data.position.toPointF();
    return grips;
}

void SurfaceFinishItem::applyGripMove(int index, const MyVector &pos) {
    prepareGeometryChange();
    // 기호/문자는 항상 정자세를 유지하므로, 지시선 그립을 드래그해도
    // rotation은 건드리지 않는다 (지시선만 자유롭게 꺾임)
    if ((m_data.leaderPoints.isEmpty() || !m_data.useLeader) && index == 0) {
        m_data.position = pos;
    } else if (index >= 0 && index < m_data.leaderPoints.size() && m_data.useLeader) {
        m_data.leaderPoints[index] = pos;
        if (index == m_data.leaderPoints.size() - 1) {
            m_data.position = pos;
        }
    }
}

QPainterPath SurfaceFinishItem::getGhostShape(int index, const MyVector &pos) const {
    SurfaceFinishItem ghost(m_data);
    ghost.setOverrideScale(getScale());
    ghost.setOverrideArrowSize(effectiveArrowSize());
    ghost.applyGripMove(index, pos);
    // shape()는 클릭 판정용으로 두껍게 부풀린 리본이라 그대로 그리면 이중선으로
    // 보이므로, 실제 모양과 같은 geometryPath()를 고스트 미리보기에 사용한다.
    return ghost.geometryPath();
}

double SurfaceFinishItem::distanceTo(const MyVector &pos) const {
    if (shape().contains(pos.toPointF())) return 0.0;
    return m_data.position.distanceTo(pos);
}

SnapPoint SurfaceFinishItem::getNearestSnap(const MyVector &pos) const {
    QList<SnapPoint> snaps = getSnapPoints();
    if (snaps.isEmpty()) return SnapPoint{m_data.position, SnapType::Endpoint};
    SnapPoint best = snaps[0];
    double minDist = pos.distanceTo(snaps[0].pos);
    for (int i = 1; i < snaps.size(); ++i) {
        double d = pos.distanceTo(snaps[i].pos);
        if (d < minDist) { minDist = d; best = snaps[i]; }
    }
    return best;
}

void SurfaceFinishItem::move(const MyVector &delta) {
    prepareGeometryChange();
    m_data.position += delta;
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) m_data.leaderPoints[i] += delta;
}

void SurfaceFinishItem::mirror(const MyVector &p1, const MyVector &p2) {
    prepareGeometryChange();
    m_data.position = MyMath::reflectPoint(m_data.position, p1, p2);
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        m_data.leaderPoints[i] = MyMath::reflectPoint(m_data.leaderPoints[i], p1, p2);
    }
    // 기호/문자는 항상 정자세를 유지하므로 지시선 방향이 아니라
    // 기존 rotation 값만 반전한다 (지시선만 자유롭게 꺾임)
    m_data.rotation = -m_data.rotation;
}

void SurfaceFinishItem::rotate(const MyVector &base, double angle) {
    prepareGeometryChange();
    m_data.position = MyMath::rotatePoint(m_data.position, base, angle);
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        m_data.leaderPoints[i] = MyMath::rotatePoint(m_data.leaderPoints[i], base, angle);
    }
    m_data.rotation = MyMath::normalizeAngle(m_data.rotation + angle);
}

void SurfaceFinishItem::scale(const MyVector &base, double factor) {
    prepareGeometryChange();
    m_data.position = MyMath::scalePoint(m_data.position, base, factor);
    for (int i = 0; i < m_data.leaderPoints.size(); ++i) {
        m_data.leaderPoints[i] = MyMath::scalePoint(m_data.leaderPoints[i], base, factor);
    }
    m_data.textHeight *= factor;
}

void SurfaceFinishItem::stretch(const QList<int> &, const MyVector &) {}

// 아래 get*Path()들은 이동/대칭/회전/축척 중 실시간 고스트 미리보기에 쓰인다.
// shape()는 클릭 판정용으로 두껍게 부풀린 리본이라 그대로 그리면 이중선으로
// 보이므로, 실제 렌더링과 같은 geometryPath()를 사용해야 한다.
QPainterPath SurfaceFinishItem::getMovedPath(const MyVector &delta) const {
    SurfaceFinishItem tmp(m_data); tmp.setOverrideScale(getScale()); tmp.setOverrideArrowSize(effectiveArrowSize()); tmp.move(delta); return tmp.geometryPath();
}
QPainterPath SurfaceFinishItem::getMirroredPath(const MyVector &p1, const MyVector &p2) const {
    SurfaceFinishItem tmp(m_data); tmp.setOverrideScale(getScale()); tmp.setOverrideArrowSize(effectiveArrowSize()); tmp.mirror(p1, p2); return tmp.geometryPath();
}
QPainterPath SurfaceFinishItem::getRotatedPath(const MyVector &base, double angle) const {
    SurfaceFinishItem tmp(m_data); tmp.setOverrideScale(getScale()); tmp.setOverrideArrowSize(effectiveArrowSize()); tmp.rotate(base, angle); return tmp.geometryPath();
}
QPainterPath SurfaceFinishItem::getScaledPath(const MyVector &base, double factor) const {
    SurfaceFinishItem tmp(m_data); tmp.setOverrideScale(getScale()); tmp.setOverrideArrowSize(effectiveArrowSize()); tmp.scale(base, factor); return tmp.geometryPath();
}
QPainterPath SurfaceFinishItem::getStretchedPath(const QList<int> &, const MyVector &) const { return geometryPath(); }

QMap<QString, QVariant> SurfaceFinishItem::getProperties() const {
    QMap<QString, QVariant> props;
    props["Type"]               = "SurfaceFinish";
    props["Layer"]              = layerName();
    props["Symbol"]             = (int)m_data.symbol;
    props["RaValue"]            = m_data.raValue;
    props["RaLetterCode"]       = m_data.raLetterCode;
    props["ProductionMethod"]   = m_data.productionMethod;
    props["CutoffValue"]        = m_data.cutoffValue;
    props["LaySymbol"]          = m_data.laySymbol;
    props["MachiningAllowance"] = m_data.machiningAllowance;
    props["LegacyGrade"]        = m_data.legacyGrade;
    props["ShowTailBar"]        = m_data.showTailBar;
    props["SymbolLayer"]        = m_data.symbolLayer;
    props["TextLayer"]          = m_data.textLayer;
    props["Rotation"]           = m_data.rotation;
    props["TextHeight"]         = m_data.textHeight;
    return props;
}

bool SurfaceFinishItem::setProperty(const QString &key, const QVariant &value) {
    if (key == "RaValue")            { m_data.raValue = value.toString(); prepareGeometryChange(); update(); return true; }
    if (key == "RaLetterCode")       { m_data.raLetterCode = value.toString(); prepareGeometryChange(); update(); return true; }
    if (key == "ProductionMethod")   { m_data.productionMethod = value.toString(); prepareGeometryChange(); update(); return true; }
    if (key == "CutoffValue")        { m_data.cutoffValue = value.toString(); update(); return true; }
    if (key == "LaySymbol")          { m_data.laySymbol = value.toString(); update(); return true; }
    if (key == "MachiningAllowance") { m_data.machiningAllowance = value.toString(); update(); return true; }
    if (key == "LegacyGrade")        { m_data.legacyGrade = qBound(1, value.toInt(), 4); prepareGeometryChange(); update(); return true; }
    if (key == "ShowTailBar")        { m_data.showTailBar = value.toBool(); prepareGeometryChange(); update(); return true; }
    if (key == "SymbolLayer")        { m_data.symbolLayer = value.toString(); update(); return true; }
    if (key == "TextLayer")          { m_data.textLayer = value.toString(); update(); return true; }
    if (key == "TextHeight")         { m_data.textHeight = value.toDouble(); prepareGeometryChange(); update(); return true; }
    return false;
}

void SurfaceFinishItem::setGripsVisible(bool) {}
