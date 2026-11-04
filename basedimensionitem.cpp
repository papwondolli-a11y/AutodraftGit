#include "basedimensionitem.h"
#include "mymath.h"
#include "mygraphicsscene.h"
#include "cadutils.h"
#include "gripitem.h"
#include "sceneprinter.h"
#include "iappcontext.h"
#include "fittolerancecalculator.h"

#include <QtMath>
#include <QCoreApplication>
#include <QPainter>
#include <QFontMetricsF>
#include <QStyleOptionGraphicsItem>

// 인쇄 중일 때만 ScenePrinter와 동일한 출력 색상 규칙(레이어별 출력색 오버라이드 우선,
// 없으면 밝은 색 자동 검정화)을 적용한다. 화면 표시 중에는 원래 색을 그대로 반환한다.
// [배경] dimLineLayer/dimTextLayer가 지정돼 있으면 paint() 도중 씬의 레이어 스타일
// 캐시에서 매번 색을 새로 읽어오는데, 이는 ScenePrinter가 아이템의 pen()만 임시로
// 바꿔서 인쇄 색상을 보정하는 방식(다른 CadItem들에 쓰는 방식)의 영향을 받지 않는다.
// 그래서 흑백 인쇄 시 치수선/치수문자만 원래 밝은 색 그대로 나와 흐리게 보였다.
static QColor withPrintColorRule(QPainter *painter, const QColor &screenColor,
                                 MyGraphicsScene *scene, const QString &layerName) {
    if (!painter || painter->device()->devType() == QInternal::Widget) {
        return screenColor;
    }
    LayerManager *lm = (scene && scene->context()) ? scene->context()->layerManager() : nullptr;
    return ScenePrinter::resolvePrintColor(screenColor, lm, layerName);
}

// 숫자 문자열을 앞쪽 기호·부호·정수부·소수부로 분해한다.
// 정수부가 순수 숫자(쉼표 제거 후)가 아니면 false를 반환한다.
struct NumericParts {
    QString symPrefix;  // Ø, R 등 앞쪽 비숫자 기호
    QString sign;       // "-" 또는 ""
    QString intDigits;  // 쉼표 없는 순수 정수 숫자열
    QString fracPart;   // ".XXX" 소수부 (없으면 "")
};

static bool parseNumeric(const QString& src, NumericParts& out)
{
    // 앞쪽 비숫자 접두사 분리
    int start = 0;
    while (start < src.size() && !src[start].isDigit() && src[start] != '-')
        ++start;
    out.symPrefix = src.left(start);
    QString rest  = src.mid(start);

    // 소수점 분리
    int dotPos    = rest.indexOf('.');
    QString intPart  = (dotPos >= 0) ? rest.left(dotPos) : rest;
    out.fracPart     = (dotPos >= 0) ? rest.mid(dotPos)  : QString();

    // 부호 분리
    out.sign = "";
    if (!intPart.isEmpty() && intPart[0] == '-') {
        out.sign = "-";
        intPart  = intPart.mid(1);
    }

    // 기존 쉼표(천 단위) 제거
    intPart.remove(',');

    // 순수 숫자인지 확인
    for (const QChar& c : intPart)
        if (!c.isDigit()) return false;

    out.intDigits = intPart;
    return true;
}

// 천 단위 쉼표를 삽입한다. 임포트 도면의 기존 쉼표도 정규화 후 재삽입한다.
static QString applyThousandSep(const QString& src)
{
    NumericParts p;
    if (!parseNumeric(src, p)) return src;   // 숫자 형식이 아니면 원본 반환

    QString intPart = p.intDigits;
    for (int i = intPart.size() - 3; i > 0; i -= 3)
        intPart.insert(i, ',');

    return p.symPrefix + p.sign + intPart + p.fracPart;
}

// 천 단위 쉼표를 제거한다 (자릿점 OFF 시 임포트 도면 정규화에 사용).
static QString stripThousandSep(const QString& src)
{
    NumericParts p;
    if (!parseNumeric(src, p)) return src;   // 숫자 형식이 아니면 원본 반환
    return p.symPrefix + p.sign + p.intDigits + p.fracPart;
}

BaseDimensionItem::BaseDimensionItem(const MyDimension &data, QGraphicsItem *parent)
    : CadItem(parent), m_data(data) {
    setZValue(10);
    setGripsEnabled(false); // 치수선 그립 마커 제거
}

BaseDimensionItem::~BaseDimensionItem() {}

QRectF BaseDimensionItem::boundingRect() const {
    if (m_isBoundingRectValid) return m_cachedBoundingRect;

    QPainterPath path = shape();
    m_cachedBoundingRect = path.boundingRect(); //.adjusted(-5, -5, 5, 5);
    m_isBoundingRectValid = true;
    return m_cachedBoundingRect;
}

void BaseDimensionItem::drawArrow(QPainter *painter, const MyVector &pos, const MyVector &direction, double arrowSize, const QColor &color,
                                   ArrowheadStyle headStyle) const {
    MyVector n(-direction.y, direction.x);
    MyVector head = pos;
    MyVector back = pos - direction * arrowSize;

    switch (headStyle) {
    case ArrowheadStyle::Open: {
        MyVector side1 = back + n * (arrowSize * 0.35);
        MyVector side2 = back - n * (arrowSize * 0.35);
        painter->setBrush(Qt::NoBrush);
        painter->drawLine(head.toPointF(), side1.toPointF());
        painter->drawLine(head.toPointF(), side2.toPointF());
        break;
    }
    case ArrowheadStyle::FilledCircle:
    case ArrowheadStyle::OpenCircle:
    case ArrowheadStyle::DoubleCircle: {
        double r = arrowSize * 0.35;
        QPointF c = pos.toPointF(); // 원의 중심을 치수선 끝점(pos)에 맞춘다.
        painter->setBrush(headStyle == ArrowheadStyle::OpenCircle ? Qt::NoBrush : QBrush(color));
        painter->drawEllipse(c, r, r);
        if (headStyle == ArrowheadStyle::DoubleCircle) {
            painter->setBrush(Qt::NoBrush);
            painter->drawEllipse(c, r * 1.7, r * 1.7);
        }
        painter->setBrush(Qt::NoBrush);
        break;
    }
    case ArrowheadStyle::Hollow: {
        // 빈화살표: Filled와 동일한 닫힌 삼각형이지만 속을 채우지 않는다(윤곽선만).
        MyVector side1 = back + n * (arrowSize * 0.35);
        MyVector side2 = back - n * (arrowSize * 0.35);
        QPolygonF poly;
        poly << head.toPointF() << side1.toPointF() << side2.toPointF();
        painter->setBrush(Qt::NoBrush);
        painter->drawPolygon(poly);
        break;
    }
    case ArrowheadStyle::Filled:
    default: {
        MyVector side1 = back + n * (arrowSize * 0.35);
        MyVector side2 = back - n * (arrowSize * 0.35);
        QPolygonF poly;
        poly << head.toPointF() << side1.toPointF() << side2.toPointF();
        painter->setBrush(color);
        painter->drawPolygon(poly);
        painter->setBrush(Qt::NoBrush);
        break;
    }
    }
}

void BaseDimensionItem::setArrow1Hidden(bool hidden) {
    if (m_data.hideArrow1 == hidden) return;
    prepareGeometryChange();
    m_data.hideArrow1 = hidden;
    m_isBoundingRectValid = false;
    update();
}

bool BaseDimensionItem::isArrow1Hidden() const { return m_data.hideArrow1; }

void BaseDimensionItem::setArrow2Hidden(bool hidden) {
    if (m_data.hideArrow2 == hidden) return;
    prepareGeometryChange();
    m_data.hideArrow2 = hidden;
    m_isBoundingRectValid = false;
    update();
}

bool BaseDimensionItem::isArrow2Hidden() const { return m_data.hideArrow2; }

void BaseDimensionItem::drawSelectionHighlight(QPainter *painter, const QStyleOptionGraphicsItem *option) const {
    // [핵심] 프린터 출력 시에는 선택 하이라이트를 그리지 않음
    if ((option->state & QStyle::State_Selected) &&
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

DimensionStyle BaseDimensionItem::getDimStyle(double &outScale) const {
    MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
    DimensionStyle style = sc ? sc->dimensionStyle() : DimensionStyle();
    if (m_data.dimScaleOverride > 0.0) {
        // DXF/DWG 임포트 시 DIMSCALE 값이 있으면 도면 스케일보다 우선 적용
        outScale = m_data.dimScaleOverride;
    } else {
        // 도면 스케일(drawingScale)을 치수 주석 크기에 반영
        double drawingScale = sc ? sc->drawingScale() : 1.0;
        outScale = (drawingScale > 0.0) ? drawingScale : 1.0;
    }
    return style;
}

LinearUnit BaseDimensionItem::sceneLinearUnit() const {
    const MyGraphicsScene *sc = dynamic_cast<const MyGraphicsScene *>(scene());
    return sc ? sc->linearUnit() : LinearUnit::Mm;
}

double BaseDimensionItem::unitDisplayFactor() const {
    if (m_data.dimensionType == DimensionType::Angular)
        return 1.0;
    return 1.0 / UnitConvert::mmPerUnit(sceneLinearUnit());
}

QString BaseDimensionItem::unitLengthSuffix() const {
    if (m_data.dimensionType == DimensionType::Angular)
        return QString();
    return UnitConvert::suffix(sceneLinearUnit());
}

void BaseDimensionItem::setupDimPen(QPainter *painter, const DimensionStyle &style, double /*viewScale*/) const {
    MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
    bool showWeight = sc ? sc->showLineWeight() : false;

    QPen currentPen = pen();
    if (!style.dimLineLayer.isEmpty() && sc) {
        LayerStyle ls = sc->getLayerStyle(style.dimLineLayer);
        currentPen.setColor(withPrintColorRule(painter, ls.color, sc, style.dimLineLayer));
    }

    if (!showWeight) {
        currentPen.setCosmetic(true);
        currentPen.setWidthF(1.0);
    } else {
        currentPen.setCosmetic(false);
        qreal scale = qAbs(painter->transform().m11());
        if (scale > 0.0 && currentPen.widthF() * scale < 1.0) {
            currentPen.setWidthF(1.0 / scale);
        }
    }
    painter->setPen(currentPen);
}

QList<SnapPoint> BaseDimensionItem::getSnapPoints() const {
    QList<SnapPoint> points;
    points << SnapPoint{m_data.start, SnapType::Endpoint};
    points << SnapPoint{m_data.end, SnapType::Endpoint};
    points << SnapPoint{m_data.dimLine, SnapType::Endpoint};
    if (m_data.dimensionType == DimensionType::Angular) {
        points << SnapPoint{m_data.end2, SnapType::Endpoint};
    }
    return points;
}

QList<QPointF> BaseDimensionItem::getGripPoints() const {
    QList<QPointF> points;
    points << m_data.start.toPointF();
    points << m_data.end.toPointF();
    points << m_data.dimLine.toPointF();
    if (m_data.dimensionType == DimensionType::Angular) {
        points << m_data.end2.toPointF();
    }
    if (m_data.useCustomTextPos) points << m_data.customTextPos.toPointF();
    return points;
}

void BaseDimensionItem::applyGripMove(int index, const MyVector &pos) {
    prepareGeometryChange();
    if (index == 0) m_data.start = pos;
    else if (index == 1) m_data.end = pos;
    else if (index == 2) m_data.dimLine = pos;
    else if (index == 3 && m_data.dimensionType == DimensionType::Angular) m_data.end2 = pos;
    else if (index == ((m_data.dimensionType == DimensionType::Angular) ? 4 : 3)) m_data.customTextPos = pos;

    m_isBoundingRectValid = false;
    updateGrips();
    update();
}

SnapPoint BaseDimensionItem::getNearestSnap(const MyVector &pos) const {
    QList<SnapPoint> snaps = getSnapPoints();
    if (snaps.isEmpty()) {
        return SnapPoint{m_data.start, SnapType::Endpoint};
    }

    SnapPoint bestSnap = snaps[0];
    double minDist = pos.distanceTo(snaps[0].pos);

    for (int i = 1; i < snaps.size(); ++i) {
        double dist = pos.distanceTo(snaps[i].pos);
        if (dist < minDist) {
            minDist = dist;
            bestSnap = snaps[i];
        }
    }
    return bestSnap;
}

void BaseDimensionItem::move(const MyVector &delta) {
    prepareGeometryChange();
    m_data.start += delta; m_data.end += delta; m_data.dimLine += delta; m_data.end2 += delta;
    if (m_data.useCustomTextPos) m_data.customTextPos += delta;
    m_isBoundingRectValid = false; updateGrips(); update();
}

void BaseDimensionItem::mirror(const MyVector &p1, const MyVector &p2) {
    prepareGeometryChange();
    m_data.start = MyMath::reflectPoint(m_data.start, p1, p2);
    m_data.end = MyMath::reflectPoint(m_data.end, p1, p2);
    m_data.dimLine = MyMath::reflectPoint(m_data.dimLine, p1, p2);
    m_data.end2 = MyMath::reflectPoint(m_data.end2, p1, p2);
    m_data.customTextPos = MyMath::reflectPoint(m_data.customTextPos, p1, p2);
    m_isBoundingRectValid = false;
    updateGrips();
    update();
}

void BaseDimensionItem::rotate(const MyVector &base, double angle) {
    prepareGeometryChange();
    m_data.start = MyMath::rotatePoint(m_data.start, base, angle);
    m_data.end = MyMath::rotatePoint(m_data.end, base, angle);
    m_data.dimLine = MyMath::rotatePoint(m_data.dimLine, base, angle);
    m_data.end2 = MyMath::rotatePoint(m_data.end2, base, angle);
    m_data.customTextPos = MyMath::rotatePoint(m_data.customTextPos, base, angle);
    m_isBoundingRectValid = false;
    updateGrips();
    update();
}

void BaseDimensionItem::scale(const MyVector &base, double scaleFactor) {
    prepareGeometryChange();
    m_data.start = MyMath::scalePoint(m_data.start, base, scaleFactor);
    m_data.end = MyMath::scalePoint(m_data.end, base, scaleFactor);
    m_data.dimLine = MyMath::scalePoint(m_data.dimLine, base, scaleFactor);
    m_data.end2 = MyMath::scalePoint(m_data.end2, base, scaleFactor);
    m_data.customTextPos = MyMath::scalePoint(m_data.customTextPos, base, scaleFactor);
    m_isBoundingRectValid = false;
    updateGrips();
    update();
}

void BaseDimensionItem::stretch(const QList<int> &indices, const MyVector &delta) {
    prepareGeometryChange();
    bool startMoved = false; bool endMoved = false; bool end2Moved = false;
    for (int i = 0; i < indices.size(); ++i) {
        int idx = indices[i];
        if (idx == 0) { m_data.start += delta; startMoved = true; }
        else if (idx == 1) { m_data.end += delta; endMoved = true; }
        else if (idx == 2) { m_data.dimLine += delta; }
        else if (idx == 3) { m_data.end2 += delta; end2Moved = true; }
    }
    if (startMoved || endMoved || end2Moved) m_data.text.clear();
    m_isBoundingRectValid = false; updateGrips(); update();
}

bool BaseDimensionItem::applyMeasurementValue(double newValue) {
    if (newValue <= 0.0) return false;
    prepareGeometryChange();
    // 원본 측정 로직 유지
    m_isBoundingRectValid = false; updateGrips(); update(); return true;
}

void BaseDimensionItem::setGripsVisible(bool visible) {
    if (visible && !gripsEnabled()) return; // 추가 가드: 그립이 비활성화된 경우 표시 요청 무시
    if (m_startGrip) m_startGrip->setVisible(visible);
    if (m_endGrip) m_endGrip->setVisible(visible);
    if (m_dimLineGrip) m_dimLineGrip->setVisible(visible);
    if (m_end2Grip) m_end2Grip->setVisible(visible);
    if (m_textGrip) m_textGrip->setVisible(visible);
}

void BaseDimensionItem::updateGrips() {
    if (!m_startGrip) m_startGrip = new GripItem(0, this);
    if (!m_endGrip) m_endGrip = new GripItem(1, this);
    if (!m_dimLineGrip) m_dimLineGrip = new GripItem(2, this);

    m_startGrip->updatePosition(m_data.start.toPointF());
    m_endGrip->updatePosition(m_data.end.toPointF());
    m_dimLineGrip->updatePosition(m_data.dimLine.toPointF());

    if (m_data.dimensionType == DimensionType::Angular) {
        if (!m_end2Grip) m_end2Grip = new GripItem(3, this);
        m_end2Grip->updatePosition(m_data.end2.toPointF());
        m_end2Grip->setVisible(m_startGrip->isVisible());
    } else if (m_end2Grip) {
        m_end2Grip->setVisible(false);
    }

    int textGripIdx = (m_data.dimensionType == DimensionType::Angular) ? 4 : 3;
    if (m_data.useCustomTextPos) {
        if (!m_textGrip) m_textGrip = new GripItem(textGripIdx, this);
        m_textGrip->updatePosition(m_data.customTextPos.toPointF());
        m_textGrip->setVisible(m_startGrip->isVisible());
    } else if (m_textGrip) {
        m_textGrip->setVisible(false);
    }
}

QVariant BaseDimensionItem::itemChange(GraphicsItemChange change, const QVariant &value) {
    if (change == QGraphicsItem::ItemSelectedChange && scene()) {
        if (!value.toBool()) setGripsVisible(false);
    }
    return CadItem::itemChange(change, value);
}

QMap<QString, QVariant> BaseDimensionItem::getProperties() const {
    QMap<QString, QVariant> props;
    props["Type"] = "Dimension";
    props["Layer"] = layerName();

    QString dimTypeStr = "Linear";
    if (m_data.dimensionType == DimensionType::Angular) dimTypeStr = "Angular";
    else if (m_data.dimensionType == DimensionType::Radial) dimTypeStr = "Radial";
    else if (m_data.dimensionType == DimensionType::Diametric) dimTypeStr = "Diametric";
    else if (m_data.dimensionType == DimensionType::HalfDimension) dimTypeStr = "HalfDimension";

    props["DimType"] = dimTypeStr;

    MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
    DimensionStyle style = sc ? sc->dimensionStyle() : DimensionStyle();

    QString txt = m_data.text;
    if (txt.isEmpty()) {
        int prec = (m_data.precision >= 0) ? m_data.precision : style.precision;
        // [Phase 5] 상속 정밀도(-1)이고 inch 도면이면 최소 자릿수를 3으로 올린다
        //   (사용자가 style.precision 을 더 크게 설정했다면 그대로 존중).
        if (m_data.precision < 0 && m_data.dimensionType != DimensionType::Angular &&
            sceneLinearUnit() == LinearUnit::Inch)
            prec = qMax(prec, UnitConvert::defaultLinearPrecision(LinearUnit::Inch));
        // [DIMLFAC] 표시 숫자에만 측정 배율 적용 (calculateTextLayout과 동일 규칙).
        // [Phase 5] 여기에 단위(mm/inch) 표시 배율을 합성한다.
        const double ms = ((m_data.measureScale > 0.0) ? m_data.measureScale : 1.0) *
                          unitDisplayFactor();

        if (m_data.dimensionType == DimensionType::Linear) {
            MyVector v = m_data.end - m_data.start;
            double measurement = 0.0;
            if (m_data.useFixedAngle) {
                double rad = qDegreesToRadians(m_data.fixedAngle);
                MyVector dir(qCos(rad), qSin(rad));
                measurement = std::abs(MyMath::dotProduct(v, dir));
            } else {
                measurement = v.magnitude();
            }
            txt = QString::number(measurement * ms, 'f', prec);
        } else if (m_data.dimensionType == DimensionType::Angular) {
            txt = QCoreApplication::translate("BaseDimensionItem", "Angular 계산 생략용"); // 원본 코드 유지
        } else if (m_data.dimensionType == DimensionType::Radial || m_data.dimensionType == DimensionType::Diametric) {
            double r = (m_data.end - m_data.start).magnitude();
            double measurement = (m_data.dimensionType == DimensionType::Diametric) ? r * 2.0 : r;
            measurement *= ms;
            if (m_data.dimensionType == DimensionType::Diametric) {
                txt = QChar(0x00D8) + QString::number(measurement, 'f', prec);
            } else {
                txt = "R" + QString::number(measurement, 'f', prec);
            }
        } else if (m_data.dimensionType == DimensionType::HalfDimension) {
            bool isHoriz = std::abs(m_data.start.x - m_data.end.x) > std::abs(m_data.start.y - m_data.end.y);
            double measurement = isHoriz ? std::abs(m_data.start.x - m_data.end.x) * 2.0 : std::abs(m_data.start.y - m_data.end.y) * 2.0;
            txt = QString::number(measurement * ms, 'f', prec);
        }
    }

    // [신규] 머리말/꼬리말이 합쳐지기 "이전"의 순수 본문값(자동측정값 또는 사용자 지정 텍스트).
    // DimTextEditDialog의 "본문" 입력칸처럼 머리말/꼬리말을 별도 필드로 편집하는 UI가
    // 이 값을 써야 한다 - "Content"(아래)를 쓰면 이미 합쳐진 문자열이 편집칸에 들어가고,
    // 그대로 저장 시 다음 렌더링에서 머리말/꼬리말이 또 한 번 붙어 중복 표기된다.
    props["RawValue"] = txt;

    txt = m_data.prefix + txt + m_data.suffix;
    props["Content"] = txt;
    props["Text"] = m_data.text;
    


    props["Prefix"] = m_data.prefix;
    props["Suffix"] = m_data.suffix;
    props["TolType"] = static_cast<int>(m_data.tolType);
    props["TolUpper"] = m_data.tolUpper;
    props["TolLower"] = m_data.tolLower;
    props["FitCode"] = m_data.fitCode; // [신규] 끼워맞춤 등급기호 (ex: "H7")
    props["FitShowValues"] = m_data.fitShowValues; // [신규] 숫자 상/하한 병기 여부
    props["TolPrecision"] = (m_data.tolPrecision >= 0) ? m_data.tolPrecision : style.tolPrecision;
    props["TolUseAngle"] = m_data.tolUseAngle;
    props["Precision"] = (m_data.precision >= 0) ? m_data.precision : style.precision;
    props["SuppressZero"] = m_data.suppressTrailingZeros;
    props["SuppressTolZero"] = m_data.suppressTrailingZerosTol;
    props["ThousandSep"] = style.useThousandSep;
    props["MeasureScale"] = m_data.measureScale; // [DIMLFAC] 측정값 표시 배율

    props["StartX"] = m_data.start.x; props["StartY"] = m_data.start.y;
    props["EndX"] = m_data.end.x; props["EndY"] = m_data.end.y;
    props["End2X"] = m_data.end2.x; props["End2Y"] = m_data.end2.y;
    props["DimLineX"] = m_data.dimLine.x; props["DimLineY"] = m_data.dimLine.y;
    props["FixedAngle"] = m_data.fixedAngle;
    props["UseFixedAngle"] = m_data.useFixedAngle;
    props["HideExt1"] = m_data.hideExt1; props["HideExt2"] = m_data.hideExt2;
    props["ExtChainH1"] = m_data.extChainH1; props["ExtChainH2"] = m_data.extChainH2;
    props["HideArrow1"] = m_data.hideArrow1; props["HideArrow2"] = m_data.hideArrow2;
    props["FlipArrow1"] = m_data.flipArrow1; props["FlipArrow2"] = m_data.flipArrow2;
    props["IsFullCircle"] = m_data.isFullCircle;

    props["UseCustomTextPos"] = m_data.useCustomTextPos;
    props["CustomTextPosX"] = m_data.customTextPos.x;
    props["CustomTextPosY"] = m_data.customTextPos.y;
    props["UseLeader"] = m_data.useLeader;

    if (sc) {
        props["DimLayer"] = sc->dimensionStyle().dimLineLayer;
        props["TextLayer"] = sc->dimensionStyle().dimTextLayer;
    } else {
        props["DimLayer"] = ""; props["TextLayer"] = "";
    }
    return props;
}

bool BaseDimensionItem::setProperty(const QString &key, const QVariant &value) {
    prepareGeometryChange();

    if (key == "Content" || key == "Text") m_data.text = value.toString();
    else if (key == "Prefix") m_data.prefix = value.toString();
    else if (key == "Suffix") m_data.suffix = value.toString();
    else if (key == "TolType") m_data.tolType = static_cast<ToleranceType>(value.toInt());
    else if (key == "FitCode") {
        m_data.fitCode = value.toString();
        // [자동 활성화 로직] 공차유형이 '없음'일 때 등급기호를 입력하면 자동으로 FitCode 유형으로 전환
        if (!m_data.fitCode.isEmpty() && m_data.tolType == ToleranceType::None) {
            m_data.tolType = ToleranceType::FitCode;
        }
    }
    else if (key == "FitShowValues") m_data.fitShowValues = value.toBool();
    else if (key == "TolUpper" || key == "TolLower") {
        double dval = value.toDouble();
        if (key == "TolUpper") m_data.tolUpper = dval;
        else m_data.tolLower = dval;
        
        // [자동 활성화 로직] 사용자가 값을 직접 입력했는데 현재 공차유형이 '없음'인 경우 자동으로 켜주기
        if (m_data.tolType == ToleranceType::None && (m_data.tolUpper != 0.0 || m_data.tolLower != 0.0)) {
            if (m_data.tolUpper == m_data.tolLower && m_data.tolUpper != 0.0) {
                m_data.tolType = ToleranceType::Symmetrical; // 대칭
            } else {
                m_data.tolType = ToleranceType::Deviation; // 편차
            }
        }
        
        // 속성창에서 공차값을 변경할 때, 해당 값의 소수점 자리수를 확인하여
        // 현재 설정된 정밀도(tolPrecision)보다 크면 자동으로 정밀도를 갱신 연동
        QString s = QString::number(std::abs(dval), 'f', 6);
        while (s.endsWith('0')) s.chop(1);
        if (s.endsWith('.')) s.chop(1);
        int dot = s.indexOf('.');
        int needed = (dot != -1) ? s.length() - dot - 1 : 0;
        if (needed > m_data.tolPrecision) m_data.tolPrecision = needed;
    }
    else if (key == "TolPrecision") m_data.tolPrecision = value.toInt();
    else if (key == "Precision") m_data.precision = value.toInt();
    else if (key == "SuppressZero") m_data.suppressTrailingZeros = value.toBool();
    else if (key == "SuppressTolZero") m_data.suppressTrailingZerosTol = value.toBool();
    else if (key == "ThousandSep") {
        MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
        if (sc) {
            DimensionStyle ds = sc->dimensionStyle();
            ds.useThousandSep = value.toBool();
            sc->setDimensionStyle(ds);
        }
    }
    else if (key == "TolUseAngle") m_data.tolUseAngle = value.toBool();
    else if (key == "HideExt1") m_data.hideExt1 = value.toBool();
    else if (key == "HideExt2") m_data.hideExt2 = value.toBool();
    else if (key == "ExtChainH1") m_data.extChainH1 = value.toDouble();
    else if (key == "ExtChainH2") m_data.extChainH2 = value.toDouble();
    else if (key == "HideArrow1") m_data.hideArrow1 = value.toBool();
    else if (key == "HideArrow2") m_data.hideArrow2 = value.toBool();
    else if (key == "FlipArrow1") m_data.flipArrow1 = value.toBool();
    else if (key == "FlipArrow2") m_data.flipArrow2 = value.toBool();
    else if (key == "FixedAngle") m_data.fixedAngle = value.toDouble();
    else if (key == "IsFullCircle") m_data.isFullCircle = value.toBool();
    else if (key == "StartX") m_data.start.x = value.toDouble();
    else if (key == "StartY") m_data.start.y = value.toDouble();
    else if (key == "EndX") m_data.end.x = value.toDouble();
    else if (key == "EndY") m_data.end.y = value.toDouble();
    else if (key == "End2X") m_data.end2.x = value.toDouble();
    else if (key == "End2Y") m_data.end2.y = value.toDouble();
    else if (key == "DimLineX") m_data.dimLine.x = value.toDouble();
    else if (key == "DimLineY") m_data.dimLine.y = value.toDouble();
    else if (key == "UseCustomTextPos") m_data.useCustomTextPos = value.toBool();
    else if (key == "CustomTextPosX") m_data.customTextPos.x = value.toDouble();
    else if (key == "CustomTextPosY") m_data.customTextPos.y = value.toDouble();
    else if (key == "UseLeader") m_data.useLeader = value.toBool();
    else if (key == "MeasureScale") {
        double v = value.toDouble();
        m_data.measureScale = (v > 0.0) ? v : 1.0;
    }
    else return false;

    m_isBoundingRectValid = false; updateGrips(); update(); return true;
}

void BaseDimensionItem::calculateTextLayout(double measurement, const QString &/*baseValueText*/, const DimensionStyle &style, double finalScale,
                                            QString &outMain, QString &outUpper, QString &outLower,
                                            double &outMainWidth, double &outTolWidth, double &outTotalWidth) const {

    // [핵심 수정] 외부 baseValueText에 의존하지 않고, 객체의 속성(m_data.precision)으로 실시간 재계산
    outMain = m_data.text;

    // [DIMLFAC] 표시 숫자에만 측정 배율 적용 (화살표/보조선 위치는 실제 기하 그대로 유지).
    // 상세도 영역에서 만든 치수는 measureScale=1/상세도축척 이라, 확대된 형상 위에서도
    // 아래의 measStr / 공차 / 한계치 계산이 모두 원본 실측값 기준으로 이뤄진다.
    if (m_data.measureScale > 0.0 && m_data.measureScale != 1.0)
        measurement *= m_data.measureScale;

    // [Phase 5] measModel = mm 기준 표시 실측값(끼워맞춤 IT등급표 조회에 사용).
    //   uf = 단위(mm/inch) 표시 배율 → measStr/공차/한계치 문자열에만 곱한다.
    const double measModel = measurement;
    const double uf = unitDisplayFactor();
    measurement *= uf;

    // 측정값 문자열 생성 (각도, 지름 기호 포함)
    // [핵심 수정] 객체 개별 정밀도(m_data.precision)가 -1이면 전역 스타일(style.precision) 상속
    int finalMainPrec = (m_data.precision >= 0) ? m_data.precision : style.precision;
    // [Phase 5] 상속 정밀도이고 inch 도면이면 최소 3자리로 올린다(사용자 지정값은 존중).
    if (m_data.precision < 0 && m_data.dimensionType != DimensionType::Angular &&
        sceneLinearUnit() == LinearUnit::Inch)
        finalMainPrec = qMax(finalMainPrec, UnitConvert::defaultLinearPrecision(LinearUnit::Inch));

    QString measStr;
    if (m_data.dimensionType == DimensionType::Angular) {
        measStr = QString::number(measurement, 'f', finalMainPrec) + QChar(0x00B0);
    } else if (m_data.dimensionType == DimensionType::Diametric) {
        measStr = QChar(0x00D8) + QString::number(measurement, 'f', finalMainPrec);
    } else if (m_data.dimensionType == DimensionType::Radial) {
        measStr = "R" + QString::number(measurement, 'f', finalMainPrec);
    } else {
        measStr = QString::number(measurement, 'f', finalMainPrec);
    }

    // [Phase 5] inch 도면이면 자동 측정 숫자 뒤에 " 부착 (mm/각도 치수는 no-op).
    measStr += unitLengthSuffix();

    if (outMain.isEmpty()) {
        outMain = measStr;
    } else {
        outMain = CadUtils::processText(outMain);
        // DXF에서 <>는 자동 측정값을 의미함. 문자열 내 어디든 포함되어 있으면 치환
        if (outMain.contains("<>")) {
            outMain.replace("<>", measStr);
        }
    }

    // 제로 억제(Trailing Zero Suppression) 처리 (70.00 -> 70)
    bool doSuppress = m_data.suppressTrailingZeros || style.suppressTrailingZeros;
    if (doSuppress && outMain.contains('.')) {
        while (outMain.endsWith('0')) outMain.chop(1);
        if (outMain.endsWith('.')) outMain.chop(1);
    }

    // 천 단위 자릿점 처리 — 각도 치수는 제외
    // 임포트 도면처럼 이미 쉼표가 들어간 텍스트를 항상 먼저 정규화(제거)한 뒤
    // useThousandSep 설정에 따라 재삽입 여부를 결정한다.
    if (m_data.dimensionType != DimensionType::Angular) {
        outMain = stripThousandSep(outMain);          // 기존 쉼표 제거 (정규화)
        if (style.useThousandSep)
            outMain = applyThousandSep(outMain);      // ON: 재삽입
    }

    // [수정] 끼워맞춤 등급기호는 더 이상 여기서 직접 붙이지 않는다. UI(속성dock/치수편집
    // 다이얼로그)에서 등급기호를 선택하면 그 텍스트를 꼬리말(suffix)에 그대로 반영해주므로,
    // 화면 표기는 항상 prefix+value+suffix 하나의 경로로만 이루어져 중복 표기 위험이 없다.
    // (fitCode 필드 자체는 아래에서 IT등급표 조회용으로만 쓰인다.)
    outMain = m_data.prefix + outMain + m_data.suffix;
    outUpper = ""; outLower = ""; outTolWidth = 0;

    int tType = static_cast<int>(m_data.tolType);
    // [핵심 수정] 공차 정밀도 또한 개별 값이 -1이면 전역 스타일을 상속
    int tolPrec = (m_data.tolPrecision >= 0) ? m_data.tolPrecision : style.tolPrecision;
    // [Phase 5] 상속 정밀도이고 inch 도면이면 공차 최소 4자리로 올린다(사용자 지정값은 존중).
    if (m_data.tolPrecision < 0 && m_data.dimensionType != DimensionType::Angular &&
        sceneLinearUnit() == LinearUnit::Inch)
        tolPrec = qMax(tolPrec, UnitConvert::defaultTolerancePrecision(LinearUnit::Inch));
    QString degSign = (m_data.tolUseAngle || m_data.dimensionType == DimensionType::Angular) ? QString(QChar(0x00B0)) : QString("");

    // [신규] 끼워맞춤 공차는 표시 직전에 IT등급표를 조회해 상/하한을 매번 다시 계산한다.
    // (그립 이동 등으로 측정값이 바뀌면 공차값도 같이 바뀌어야 하므로 m_data에 캐시하지 않음)
    double effUpper = m_data.tolUpper;
    double effLower = m_data.tolLower;
    if (m_data.tolType == ToleranceType::FitCode) {
        // IT등급표 조회는 mm 공칭값(measModel)으로 수행한다.
        FitToleranceResult fitResult = FitToleranceCalculator::calculate(measModel, m_data.fitCode);
        if (fitResult.valid) {
            effUpper = fitResult.upper;
            effLower = fitResult.lower;
        }
    }
    // [Phase 5] 공차 표시값도 화면 단위로 환산 (tType 1/2 표기, tType 3 한계치 공통).
    effUpper *= uf;
    effLower *= uf;

    QFont mainFont; mainFont.setFamily(style.fontFamily); mainFont.setPointSizeF(style.textSize * finalScale);
    QFontMetricsF mainFm(mainFont);
    outMainWidth = mainFm.horizontalAdvance(outMain);

    if (tType != 0) {
        if (tType == 1) {
            outUpper = QString("±%1%2").arg(effUpper, 0, 'f', tolPrec).arg(degSign);
        } else if (tType == 2 || (tType == 4 && m_data.fitShowValues)) {
            // tType==2: 편차(Deviation). tType==4(끼워맞춤)는 fitShowValues가 켜져 있을 때만
            // 숫자 상/하한을 병기한다 - 실무 관례상 기본은 "30H7"처럼 기호만 표기하고
            // 숫자는 생략하므로, fitShowValues==false면 이 분기를 타지 않아 outUpper/outLower가
            // 빈 문자열로 남는다(= 위쪽의 outMain에 붙은 fitSuffixForMain만 화면에 보임).
            QString uStr = QString("%1").arg(std::abs(effUpper), 0, 'f', tolPrec);
            if (effUpper > 0) outUpper = "+" + uStr + degSign;
            else if (effUpper < 0) outUpper = "-" + uStr + degSign;
            else outUpper = " " + uStr + degSign;

            QString lStr = QString("%1").arg(std::abs(effLower), 0, 'f', tolPrec);
            if (effLower > 0) outLower = "-" + lStr + degSign;
            else if (effLower < 0) outLower = "+" + lStr + degSign;
            else outLower = "-" + lStr + degSign;
        } else if (tType == 3) {
            outUpper = QString("%1%2").arg(measurement + effUpper, 0, 'f', tolPrec).arg(degSign);
            outLower = QString("%1%2").arg(measurement - effLower, 0, 'f', tolPrec).arg(degSign);
            outMain = m_data.prefix;
            outMainWidth = mainFm.horizontalAdvance(outMain);
        }

        // [추가] 공차 제로 억제 처리 (DIMTZIN)
        bool doSuppressTol = m_data.suppressTrailingZerosTol || style.suppressTrailingZerosTol;
        if (doSuppressTol) {
            auto suppress = [](QString& s, const QString& ds) {
                if (!s.contains('.')) return;
                bool hasDeg = !ds.isEmpty() && s.endsWith(ds);
                if (hasDeg) s.chop(ds.length());
                while (s.endsWith('0')) s.chop(1);
                if (s.endsWith('.')) s.chop(1);
                if (hasDeg) s += ds;
            };
            suppress(outUpper, degSign);
            suppress(outLower, degSign);
        }

        QFont tolFont; tolFont.setFamily(style.fontFamily); tolFont.setPointSizeF(style.toleranceSize * finalScale);
        QFontMetricsF tolFm(tolFont);
        if (tType == 1) outTolWidth = tolFm.horizontalAdvance(outUpper);
        else outTolWidth = qMax(tolFm.horizontalAdvance(outUpper), tolFm.horizontalAdvance(outLower));
    }
    double hGap = (outTolWidth > 0) ? (0.4 * finalScale) : 0.0;
    outTotalWidth = outMainWidth + hGap + outTolWidth;
}

void BaseDimensionItem::drawDimensionText(QPainter *painter, double measurement, const QString &baseValueText,
                                          const MyVector &pos, double angle, const DimensionStyle &style, double finalScale) const {

    MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
    QString mainText, upperText, lowerText;
    double mainW, tolW, totalW;
    calculateTextLayout(measurement, baseValueText, style, finalScale, mainText, upperText, lowerText, mainW, tolW, totalW);

    QFont mainFont(style.fontFamily, style.textSize * finalScale);
    QFont tolFont(style.fontFamily, style.toleranceSize * finalScale);
    QFontMetricsF mainFm(mainFont);
    QFontMetricsF tolFm(tolFont);

    QColor textColor = pen().color();
    if (!style.dimTextLayer.isEmpty() && sc) {
        textColor = withPrintColorRule(painter, sc->getLayerStyle(style.dimTextLayer).color, sc, style.dimTextLayer);
    }

    QPen textPen(painter->pen().color());
    textPen.setCosmetic(true);
    textPen.setColor(textColor);

    painter->save();

    double startX = -totalW / 2.0;
    QPointF targetPos = pos.toPointF();
    double renderAngle = angle;

    if (m_data.useCustomTextPos) {
        if (!m_data.useLeader) {
            double rad = qDegreesToRadians(angle);
            MyVector dir(qCos(rad), qSin(rad));
            double t = MyMath::dotProduct(m_data.customTextPos - pos, dir);
            targetPos = (pos + dir * t).toPointF();
        } else {
            targetPos = m_data.customTextPos.toPointF();
            renderAngle = 0.0;

            double sign = (m_data.customTextPos.x >= pos.x) ? 1.0 : -1.0;
            if (sign > 0) startX = 1.0 * finalScale;
            else startX = -totalW - 1.0 * finalScale;
        }
    }

    painter->translate(targetPos);
    painter->rotate(renderAngle);
    painter->scale(1, -1);
    painter->setPen(textPen);

    auto drawPathText = [&](const QPointF &pt, const QFont &f, const QString &t) {
        QPainterPath p;
        p.addText(pt, f, t);
        painter->fillPath(p, textPen.color());
    };

    double baselineY = -style.textGap * finalScale - mainFm.descent();
    drawPathText(QPointF(startX, baselineY), mainFont, mainText);

    int tType = static_cast<int>(m_data.tolType);
    if (tType != 0 && tolW > 0) {
        double hGap = 0.4 * finalScale;
        double tolX = startX + mainW + hGap;

        if (tType == 1) {
            drawPathText(QPointF(tolX, baselineY), tolFont, upperText);
        } else if (tType == 2 || tType == 3 || tType == 4) {
            double mainCenterY = baselineY - (mainFm.ascent() / 2.0);
            double vGap = 0.15 * finalScale;
            drawPathText(QPointF(tolX, mainCenterY - vGap), tolFont, upperText);
            drawPathText(QPointF(tolX, mainCenterY + tolFm.ascent() + vGap), tolFont, lowerText);
        }
    }
    painter->restore();
}

QPainterPath BaseDimensionItem::createTextBoundingPath(double measurement, const QString& baseValueText,
                                                       const MyVector& pos, double angle) const {
    MyGraphicsScene *sc = dynamic_cast<MyGraphicsScene *>(scene());
    DimensionStyle style = sc ? sc->dimensionStyle() : DimensionStyle();
    double finalScale;
    if (m_data.dimScaleOverride > 0.0) {
        finalScale = m_data.dimScaleOverride;
    } else {
        double drawingScale = sc ? sc->drawingScale() : 1.0;
        finalScale = (drawingScale > 0.0) ? drawingScale : 1.0;
    }

    QString mainText, upperText, lowerText;
    double mainW, tolW, totalW;
    calculateTextLayout(measurement, baseValueText, style, finalScale, mainText, upperText, lowerText, mainW, tolW, totalW);

    QFont mainFont(style.fontFamily, style.textSize * finalScale);
    QFontMetricsF mainFm(mainFont);
    QFont tolFont(style.fontFamily, style.toleranceSize * finalScale);
    QFontMetricsF tolFm(tolFont);

    double baselineY = -style.textGap * finalScale - mainFm.descent();
    double top = baselineY - mainFm.ascent();
    double bottom = baselineY + mainFm.descent();

    int tType = static_cast<int>(m_data.tolType);
    if ((tType == 2 || tType == 3 || tType == 4) && tolW > 0) {
        double mainCenterY = baselineY - (mainFm.ascent() / 2.0);
        double vGap = 0.15 * finalScale;
        double tolTop = mainCenterY - vGap - tolFm.ascent();
        double tolBottom = mainCenterY + tolFm.ascent() + vGap + tolFm.descent();
        if (tolTop < top) top = tolTop;
        if (tolBottom > bottom) bottom = tolBottom;
    }

    if (!m_data.useCustomTextPos) {
        QTransform t; t.translate(pos.x, pos.y); t.rotate(angle); t.scale(1, -1);
        QRectF textRect(-totalW / 2.0 - 2, top - 2, totalW + 4, bottom - top + 4);
        QPolygonF poly;
        poly << t.map(textRect.topLeft()) << t.map(textRect.bottomLeft()) << t.map(textRect.bottomRight()) << t.map(textRect.topRight());
        QPainterPath path; path.addPolygon(poly); path.closeSubpath();
        return path;
    } else if (!m_data.useLeader) {
        double rad = qDegreesToRadians(angle);
        MyVector dir(qCos(rad), qSin(rad));
        double t = MyMath::dotProduct(m_data.customTextPos - pos, dir);
        MyVector targetPos = pos + dir * t;

        QTransform tTrans; tTrans.translate(targetPos.x, targetPos.y); tTrans.rotate(angle); tTrans.scale(1, -1);
        QRectF textRect(-totalW / 2.0 - 2, top - 2, totalW + 4, bottom - top + 4);
        QPolygonF poly;
        poly << tTrans.map(textRect.topLeft()) << tTrans.map(textRect.bottomLeft()) << tTrans.map(textRect.bottomRight()) << tTrans.map(textRect.topRight());
        QPainterPath path; path.addPolygon(poly); path.closeSubpath();
        return path;
    } else {
        double sign = (m_data.customTextPos.x >= pos.x) ? 1.0 : -1.0;
        double hPadding = 2.0 * finalScale;
        double landingWidth = totalW + hPadding;

        QTransform tTrans; tTrans.translate(m_data.customTextPos.x, m_data.customTextPos.y); tTrans.scale(1, -1);
        double startX = (sign > 0) ? 0.0 : -totalW - hPadding;
        QRectF textRect(startX - 2, top - 2, totalW + 4, bottom - top + 4);

        QPolygonF poly;
        poly << tTrans.map(textRect.topLeft()) << tTrans.map(textRect.bottomLeft()) << tTrans.map(textRect.bottomRight()) << tTrans.map(textRect.topRight());
        QPainterPath path; path.addPolygon(poly); path.closeSubpath();

        QPainterPath linePath;
        linePath.moveTo(pos.toPointF());
        linePath.lineTo(m_data.customTextPos.toPointF());
        linePath.lineTo(QPointF(m_data.customTextPos.x + sign * landingWidth, m_data.customTextPos.y));

        QPainterPathStroker stroker; stroker.setWidth(4.0);
        path.addPath(stroker.createStroke(linePath));
        return path;
    }
}