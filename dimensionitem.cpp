#include "dimensionitem.h"
#include "cadutils.h"
#include "arcitem.h"
#include "gripitem.h"
#include "lineitem.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "polylineitem.h"
#include "textitem.h"
#include "sceneprinter.h"
#include "iappcontext.h"
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QtMath>

// 인쇄 중일 때만 ScenePrinter와 동일한 출력 색상 규칙(레이어별 출력색 오버라이드 우선,
// 없으면 밝은 색 자동 검정화)을 적용한다. 화면 표시 중에는 원래 색을 그대로 반환한다.
// (basedimensionitem.cpp의 withPrintColorRule과 동일한 이유로 필요 - dimLineLayer/
// dimTextLayer가 지정되면 paint() 도중 레이어 스타일을 매번 새로 읽어오기 때문에,
// ScenePrinter가 아이템의 pen()만 임시로 바꾸는 방식으로는 영향을 줄 수 없다.)
static QColor withPrintColorRule(QPainter *painter, const QColor &screenColor,
                                 MyGraphicsScene *scene, const QString &layerName) {
    if (!painter || painter->device()->devType() == QInternal::Widget) {
        return screenColor;
    }
    LayerManager *lm = (scene && scene->context()) ? scene->context()->layerManager() : nullptr;
    return ScenePrinter::resolvePrintColor(screenColor, lm, layerName);
}

DimensionItem::DimensionItem(const MyDimension &data, QGraphicsItem *parent)
    : CadItem(parent), m_data(data) {
  setZValue(10);
  DimensionItem::setGripsVisible(false); // Default: hide grips
  updateGrips();
}

DimensionItem::~DimensionItem() {}

// [수정] 레이어·펜을 함께 복사 (누락 시 복사본이 기본 펜/씬 루트로 붙음)
CadItem *DimensionItem::clone() const {
  auto *item = new DimensionItem(m_data);
  item->setLayerName(layerName());
  item->setPen(m_pen);
  return item;
}

QRectF DimensionItem::boundingRect() const {
  if (m_isBoundingRectValid)
    return m_cachedBoundingRect;

  QPainterPath path = geometryPath();
  m_cachedBoundingRect = path.boundingRect().adjusted(-5, -5, 5, 5);
  m_isBoundingRectValid = true;
  return m_cachedBoundingRect;
}

void DimensionItem::paint(QPainter *painter,
                          const QStyleOptionGraphicsItem *option,
                          QWidget *widget) {
  Q_UNUSED(widget);

  MyGraphicsScene *m_scene = qobject_cast<MyGraphicsScene *>(scene());

  // 0. 하이라이트 배경 그리기
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

  double sceneScale = m_scene ? m_scene->drawingScale() : 1.0;
  DimensionStyle style = m_scene ? m_scene->dimensionStyle() : DimensionStyle();

  // [수정] 텍스트와 화살표가 일반 텍스트처럼 줌에 따라 커지게 하려면
  // viewScale(줌 역산)을 곱하지 않고 도면 축척(sceneScale)만 반영합니다.
  double finalScale = sceneScale;

  // 줌 레벨과 관계없이 선 두께를 일정하게 유지하기 위한 뷰 스케일
  double viewScale = 1.0;
  if (!painter->transform().isIdentity()) {
    double m11abs = qAbs(painter->transform().m11());
    if (m11abs > 0.0) viewScale = 1.0 / m11abs;
  }

  QPen basePen = pen();
  // Setting 메뉴의 Line Weight 적용 여부 확인
  if (m_scene && !m_scene->showLineWeight()) {
    basePen.setWidthF(0);
  } else {
    // 선 두께는 줌에 관계없이 시각적으로 일정하게 유지 (코스메틱)
    basePen.setWidthF(basePen.widthF() * viewScale);
  }

  // 1. 치수선용 펜 설정 (스타일의 레이어 반영)
  QPen linePen = basePen;
  if (!style.dimLineLayer.isEmpty() && m_scene) {
    LayerStyle ls = m_scene->getLayerStyle(style.dimLineLayer);
    linePen.setColor(withPrintColorRule(painter, ls.color, m_scene, style.dimLineLayer));
  }
  painter->setPen(linePen);

  MyVector p1 = m_data.start;
  MyVector p2 = m_data.end;
  MyVector p3 = m_data.dimLine;

  if (m_data.dimensionType == DimensionType::Linear) {
    // Calculate Geometry
    MyVector v = p2 - p1;
    double len = v.magnitude();
    if (len < 1e-6)
      return;

    MyVector dir;
    if (m_data.useFixedAngle) {
      double rad = qDegreesToRadians(m_data.fixedAngle);
      dir = MyVector(qCos(rad), qSin(rad));
    } else {
      dir = v / len;
    }
    MyVector normal(-dir.y, dir.x);
    MyVector v3 = p3 - p1;
    double h = MyMath::dotProduct(v3, normal);

    MyVector d1 = p1 + normal * h;
    double measurement = len;
    if (m_data.useFixedAngle) {
      measurement = std::abs(MyMath::dotProduct(p2 - p1, dir));
    }
    MyVector d2 =
        d1 +
        dir * (m_data.useFixedAngle ? MyMath::dotProduct(p2 - p1, dir) : len);

    // 2. Draw Extension Lines
    if (std::abs(h) > 1e-6) {
      double extOffset = style.extOffset * finalScale;
      double extExtend = style.extExtend * finalScale;

      if (!m_data.hideExt1) {
        MyVector extStart1 = p1 + normal * (h > 0 ? extOffset : -extOffset);
        MyVector extEnd1 = d1 + normal * (h > 0 ? extExtend : -extExtend);
        painter->drawLine(extStart1.toPointF(), extEnd1.toPointF());
      }

      if (!m_data.hideExt2) {
        MyVector extStart2 = p2 + normal * (h > 0 ? extOffset : -extOffset);
        MyVector extEnd2 = d2 + normal * (h > 0 ? extExtend : -extExtend);
        painter->drawLine(extStart2.toPointF(), extEnd2.toPointF());
      }
    }

    // 3. Draw Dimension Line
    painter->drawLine(d1.toPointF(), d2.toPointF());

    // 4. Draw Arrows
    double arrowSize = style.arrowSize * finalScale;
    auto drawArrow = [&](const MyVector &pos, const MyVector &direction) {
      MyVector n(-direction.y, direction.x);
      MyVector head = pos;
      MyVector back = pos - direction * arrowSize;
      MyVector side1 = back + n * (arrowSize * 0.35);
      MyVector side2 = back - n * (arrowSize * 0.35);

      QPolygonF poly;
      poly << head.toPointF() << side1.toPointF() << side2.toPointF();
      painter->setBrush(linePen.color());
      painter->drawPolygon(poly);
      painter->setBrush(Qt::NoBrush);
    };

    drawArrow(d1, dir * -1.0);
    drawArrow(d2, dir);

    // 5. Draw Text
    QString txt = m_data.text;
    if (txt.isEmpty()) {
      txt = QString::number(measurement, 'f', style.precision);
    } else {
      txt = CadUtils::processText(txt);
    }

    // 텍스트용 레이어 색성 적용
    QColor textColor = basePen.color();
    if (!style.dimTextLayer.isEmpty() && m_scene) {
      textColor = withPrintColorRule(painter, m_scene->getLayerStyle(style.dimTextLayer).color, m_scene, style.dimTextLayer);
    }
    painter->setPen(textColor);

    QFont font = painter->font();
    font.setFamily(style.fontFamily);
    font.setPointSizeF(style.textSize * finalScale);
    painter->setFont(font);

    MyVector mid = (d1 + d2) * 0.5;
    double textGap = style.textGap * finalScale;

//    double angle = qRadiansToDegrees(qAtan2(dir.y, dir.x));
    MyVector textNormal = normal;

    if (qAbs(dir.y) > qAbs(dir.x)) {
      if (textNormal.x > 1e-9) {
        textNormal = textNormal * -1.0;
        dir = dir * -1.0;
      }
    } else {
      if (textNormal.y < -1e-9) {
        textNormal = textNormal * -1.0;
        dir = dir * -1.0;
      }
    }
    double angle = qRadiansToDegrees(qAtan2(dir.y, dir.x));

    MyVector textPos = mid + textNormal * textGap;

    painter->save();
    painter->translate(textPos.toPointF());
    painter->rotate(angle);
    painter->scale(1, -1);

    QFontMetricsF fm(font);
    double textWidth = fm.horizontalAdvance(txt);

    // [핵심] textPos가 텍스트의 정중앙이므로,
    // X축은 폭의 절반만큼 왼쪽으로 이동, Y축은 텍스트 높이의 절반만큼 아래(베이스라인)로 내립니다.
    double baselineY = -(fm.ascent() - fm.descent()) / 2.0;
    painter->drawText(QPointF(-textWidth / 2.0, baselineY), txt);

    // QRectF textRect = painter->fontMetrics().boundingRect(txt);
    // textRect.adjust(-5, 0, 5, 0);

    // painter->drawText(QRectF(-textRect.width() / 2, -textRect.height() / 2,
    //                          textRect.width(), textRect.height()),
    //                   Qt::AlignCenter, txt);
    painter->restore();
  } else if (m_data.dimensionType == DimensionType::Radial ||
             m_data.dimensionType == DimensionType::Diametric) {
    // p1: center, p2: point on circle (radius ref), p3: placement
    double radius = (p2 - p1).magnitude();
    if (radius < 1e-6)
      return;

    MyVector dir = (p3 - p1);
    double dist = dir.magnitude();
    if (dist < 1e-6)
      dir = MyVector(1, 0);
    else
      dir = dir / dist;

    bool isOutside = dist > radius;
    MyVector q1 = p1 + dir * radius; // intersection 1
    MyVector q2 = p1 - dir * radius; // intersection 2 (opposite)

    double measurement = (m_data.dimensionType == DimensionType::Diametric)
                             ? radius * 2.0
                             : radius;
    QString prefix = (m_data.dimensionType == DimensionType::Diametric)
                         ? QString::fromUtf8("Ø")
                         : "R";

    double arrowSize = style.arrowSize * finalScale;
    auto drawArrow = [&](const MyVector &pos, const MyVector &direction) {
      MyVector n(-direction.y, direction.x);
      MyVector head = pos;
      MyVector back = pos - direction * arrowSize;
      MyVector side1 = back + n * (arrowSize * 0.35);
      MyVector side2 = back - n * (arrowSize * 0.35);

      QPolygonF poly;
      poly << head.toPointF() << side1.toPointF() << side2.toPointF();
      painter->setBrush(linePen.color());
      painter->drawPolygon(poly);
      painter->setBrush(Qt::NoBrush);
    };

    MyVector lineStart, lineEnd, textBase;
    double extExtend = style.extExtend * finalScale;

    if (m_data.dimensionType == DimensionType::Diametric) {
      if (isOutside) {
        lineStart = q2 - dir * (arrowSize + extExtend);
        lineEnd = p3 + dir * extExtend;
        textBase = p3;
        drawArrow(q1, dir * -1.0); // Point IN
        drawArrow(q2, dir);        // Point IN
      } else {
        lineStart = q2;
        lineEnd = q1;
        textBase = p1;             // Center of circle
        drawArrow(q1, dir);        // Point OUT
        drawArrow(q2, dir * -1.0); // Point OUT
      }
    } else { // Radial
      if (isOutside) {
        lineStart = p1;
        lineEnd = p3 + dir * (arrowSize + extExtend);
        textBase = p3;
        drawArrow(q1, dir * -1.0); // Point IN
      } else {
        lineStart = p1;
        lineEnd = q1;
        textBase = (p1 + q1) * 0.5;
        drawArrow(q1, dir); // Point OUT
      }
    }

    // Draw main line
    painter->drawLine(lineStart.toPointF(), lineEnd.toPointF());

    // Draw Text
    QString txt = m_data.text;
    if (txt.isEmpty()) {
      txt = prefix + QString::number(measurement, 'f', style.precision);
    } else {
      txt = CadUtils::processText(txt);
    }

    QColor textColor = basePen.color();
    if (!style.dimTextLayer.isEmpty() && m_scene) {
      textColor = withPrintColorRule(painter, m_scene->getLayerStyle(style.dimTextLayer).color, m_scene, style.dimTextLayer);
    }
    painter->setPen(textColor);

    QFont font = painter->font();
    font.setFamily(style.fontFamily);
    font.setPointSizeF(style.textSize * finalScale);
    painter->setFont(font);

    double textGap = style.textGap * finalScale;
    MyVector textDir = dir;
    MyVector textNormal(-textDir.y, textDir.x);

    // Readability logic: keep text upright and usually above/left of the line
    if (qAbs(textDir.y) > qAbs(textDir.x)) {
      if (textNormal.x > 1e-9) {
        textNormal = textNormal * -1.0;
        textDir = textDir * -1.0;
      }
    } else {
      if (textNormal.y < -1e-9) {
        textNormal = textNormal * -1.0;
        textDir = textDir * -1.0;
      }
    }
    double angle = qRadiansToDegrees(qAtan2(textDir.y, textDir.x));
    MyVector textPos = textBase + textNormal * textGap;

    painter->save();
    painter->translate(textPos.toPointF());
    painter->rotate(angle);
    painter->scale(1, -1);

    QRectF textRect = painter->fontMetrics().boundingRect(txt);
    textRect.adjust(-5, 0, 5, 0);
    painter->drawText(QRectF(-textRect.width() / 2, -textRect.height() / 2,
                             textRect.width(), textRect.height()),
                      Qt::AlignCenter, txt);
    painter->restore();
  } else if (m_data.dimensionType == DimensionType::Angular) {
    MyVector vertex = m_data.start;
    MyVector p1 = m_data.end;
    MyVector p2 = m_data.end2;
    MyVector pArc = m_data.dimLine;

    double radius = (pArc - vertex).magnitude();
    if (radius < 1e-6)
      return;

    double ang1 = qRadiansToDegrees(qAtan2(p1.y - vertex.y, p1.x - vertex.x));
    double ang2 = qRadiansToDegrees(qAtan2(p2.y - vertex.y, p2.x - vertex.x));
    double angArc =
        qRadiansToDegrees(qAtan2(pArc.y - vertex.y, pArc.x - vertex.x));

    ang1 = MyMath::normalizeAngle(ang1);
    ang2 = MyMath::normalizeAngle(ang2);
    angArc = MyMath::normalizeAngle(angArc);

    double startAng = ang1;
    double endAng = ang2;
    double span = endAng - startAng;

    if (span < 0)
      span += 360.0;
    if (span > 360.0)
      span -= 360.0;

    // Determine which side of the angle to dimension based on pArc
    if (!MyMath::isAngleBetween(angArc, startAng, span)) {
      startAng = ang2;
      endAng = ang1;
      span = endAng - startAng;
      if (span < 0)
        span += 360.0;
    }

    double measurement = span;

    // Draw Arc
    QRectF arcRect(vertex.x - radius, vertex.y - radius, radius * 2,
                   radius * 2);
    painter->drawArc(arcRect, qRound(-startAng * 16), qRound(-span * 16));

    // Draw Arrows
    double arrowSize = style.arrowSize * finalScale;
    auto drawArrow = [&](double angle, bool clockwise) {
      double rad = qDegreesToRadians(angle);
      MyVector pos(vertex.x + qCos(rad) * radius,
                   vertex.y + qSin(rad) * radius);
      MyVector dir(qSin(rad), -qCos(rad)); // Tangent
      if (clockwise)
        dir = dir * -1.0;

      MyVector n(-dir.y, dir.x);
      MyVector head = pos;
      MyVector back = pos - dir * arrowSize;
      MyVector side1 = back + n * (arrowSize * 0.35);
      MyVector side2 = back - n * (arrowSize * 0.35);

      QPolygonF poly;
      poly << head.toPointF() << side1.toPointF() << side2.toPointF();
      painter->setBrush(linePen.color());
      painter->drawPolygon(poly);
      painter->setBrush(Qt::NoBrush);
    };

    drawArrow(startAng, false);
    drawArrow(startAng + span, true);

    // Draw Text
    QString txt = m_data.text;
    if (txt.isEmpty()) {
      txt = QString::number(measurement, 'f', style.precision) +
            QString::fromUtf8("°");
    } else {
      txt = CadUtils::processText(txt);
    }

    QColor textColor = basePen.color();
    if (!style.dimTextLayer.isEmpty() && m_scene) {
      textColor = withPrintColorRule(painter, m_scene->getLayerStyle(style.dimTextLayer).color, m_scene, style.dimTextLayer);
    }
    painter->setPen(textColor);

    QFont font = painter->font();
    font.setFamily(style.fontFamily);
    font.setPointSizeF(style.textSize * finalScale);
    painter->setFont(font);

    double midAng = startAng + span * 0.5;
    double textGap = style.textGap * finalScale;
    double radMid = qDegreesToRadians(midAng);
    MyVector textPos(vertex.x + qCos(radMid) * (radius + textGap),
                     vertex.y + qSin(radMid) * (radius + textGap));

    painter->save();
    painter->translate(textPos.toPointF());
    // Keep text upright
    double rotation = midAng + 90.0;
    if (rotation > 90.0 && rotation < 270.0)
        rotation -= 180.0;
    
    painter->rotate(rotation);
    painter->scale(1, -1);

    QRectF textRect = painter->fontMetrics().boundingRect(txt);
    textRect.adjust(-5, 0, 5, 0);
    painter->drawText(QRectF(-textRect.width() / 2, -textRect.height() / 2,
                             textRect.width(), textRect.height()),
                      Qt::AlignCenter, txt);
    painter->restore();

    // Draw Extension Lines
    if (m_scene) {
      double extOffset = style.extOffset * finalScale;
      double extExtend = style.extExtend * finalScale;

      auto drawExt = [&](const MyVector &pt, double angle) {
        double rad = qDegreesToRadians(angle);
        MyVector dir(qCos(rad), qSin(rad));
        double dist = (pt - vertex).magnitude();
        if (radius > dist) {
          MyVector s = pt + dir * extOffset;
          MyVector e = vertex + dir * (radius + extExtend);
          painter->drawLine(s.toPointF(), e.toPointF());
        }
      };

      if (!m_data.hideExt1)
        drawExt(p1, ang1);
      if (!m_data.hideExt2)
        drawExt(p2, ang2);
    }
  }
}

QList<CadItem *> DimensionItem::explode() const {
  QList<CadItem *> items;
  MyGraphicsScene *scenePtr = qobject_cast<MyGraphicsScene *>(scene());
  if (!scenePtr)
    return items;

  double sceneScale = scenePtr->drawingScale();
  DimensionStyle style = scenePtr->dimensionStyle();
  double finalScale = sceneScale;

  // 헬퍼: 레이어 스타일에 맞춰 아이템의 펜과 레이어명 설정
  auto applyItemStyle = [&](CadItem *item, const QString &layerName) {
    QString targetLayer = layerName.isEmpty() ? m_layerName : layerName;
    LayerStyle ls = scenePtr->getLayerStyle(targetLayer);
    QPen p(ls.color);
    p.setCosmetic(true);
    p.setWidthF(1.0); // 기본 코스메틱 두께
    if (!ls.dashPattern.isEmpty()) {
      p.setStyle(Qt::CustomDashLine);
      p.setDashPattern(ls.dashPattern);
    }
    item->setLayerName(targetLayer);
    item->setPen(p);
    return ls;
  };

  MyVector p1 = m_data.start;
  MyVector p2 = m_data.end;
  MyVector p3 = m_data.dimLine;

  if (m_data.dimensionType == DimensionType::Linear) {
    MyVector v = p2 - p1;
    double len = v.magnitude();
    if (len < 1e-6)
      return items;

    MyVector dir;
    if (m_data.useFixedAngle) {
      double rad = qDegreesToRadians(m_data.fixedAngle);
      dir = MyVector(qCos(rad), qSin(rad));
    } else {
      dir = v / len;
    }
    MyVector normal(-dir.y, dir.x);
    MyVector v3 = p3 - p1;
    double h = MyMath::dotProduct(v3, normal);

    MyVector d1 = p1 + normal * h;
    double measurement = len;
    if (m_data.useFixedAngle) {
      measurement = std::abs(MyMath::dotProduct(p2 - p1, dir));
    }
    MyVector d2 =
        d1 +
        dir * (m_data.useFixedAngle ? MyMath::dotProduct(p2 - p1, dir) : len);

    // 1. Extension Lines
    if (std::abs(h) > 1e-6) {
      double extOffset = style.extOffset * finalScale;
      double extExtend = style.extExtend * finalScale;

      if (!m_data.hideExt1) {
        MyVector extStart1 = p1 + normal * (h > 0 ? extOffset : -extOffset);
        MyVector extEnd1 = d1 + normal * (h > 0 ? extExtend : -extExtend);
        LineItem *l1 = new LineItem(extStart1, extEnd1);
        applyItemStyle(l1, style.dimLineLayer);
        items << l1;
      }

      if (!m_data.hideExt2) {
        MyVector extStart2 = p2 + normal * (h > 0 ? extOffset : -extOffset);
        MyVector extEnd2 = d2 + normal * (h > 0 ? extExtend : -extExtend);
        LineItem *l2 = new LineItem(extStart2, extEnd2);
        applyItemStyle(l2, style.dimLineLayer);
        items << l2;
      }
    }

    // 2. Dimension Line
    LineItem *dl = new LineItem(d1, d2);
    applyItemStyle(dl, style.dimLineLayer);
    items << dl;

    // 3. Arrows
    double arrowSize = style.arrowSize * finalScale;
    auto addArrow = [&](const MyVector &pos, const MyVector &direction) {
      MyVector n(-direction.y, direction.x);
      MyVector back = pos - direction * arrowSize;
      MyVector side1 = back + n * (arrowSize * 0.35);
      MyVector side2 = back - n * (arrowSize * 0.35);

      PolylineItem *arrow = new PolylineItem({pos, side1, side2}, true);
      LayerStyle ls = applyItemStyle(arrow, style.dimLineLayer);
      arrow->setBrush(QBrush(ls.color)); // 화살표 채우기 적용
      items << arrow;
    };
    addArrow(d1, dir * -1.0);
    addArrow(d2, dir);

    // 4. Text
    QString txt = m_data.text;
    if (txt.isEmpty()) {
      txt = QString::number(measurement, 'f', style.precision);
    } else {
      txt = CadUtils::processText(txt);
    }

    double textGap = style.textGap * finalScale;
//    double angleDeg = qRadiansToDegrees(qAtan2(dir.y, dir.x));
    MyVector textNormal = normal;

    if (qAbs(dir.y) > qAbs(dir.x)) {
      if (textNormal.x > 1e-9) {
        textNormal = textNormal * -1.0;
        dir = dir * -1.0;
      }
    } else {
      if (textNormal.y < -1e-9) {
        textNormal = textNormal * -1.0;
        dir = dir * -1.0;
      }
    }
 double  angleDeg = qRadiansToDegrees(qAtan2(dir.y, dir.x));
    MyVector textCenter = (d1 + d2) * 0.5;
    MyVector textPos = textCenter + textNormal * textGap;

    MyText textData;
    textData.text = txt;
    textData.pos = textPos;
    textData.rotation = angleDeg;
    textData.fontSize = style.textSize * finalScale;
    textData.fontFamily = style.fontFamily;

    TextItem *ti = new TextItem(textData);
    LayerStyle tls = applyItemStyle(ti, style.dimTextLayer);
    ti->setPen(QPen(tls.color)); // 텍스트 색상 강제 지정 (CadItem::paint 호환)
    items << ti;
  } else if (m_data.dimensionType == DimensionType::Radial ||
             m_data.dimensionType == DimensionType::Diametric) {
    // Radial or Diametric
    double radius = (p2 - p1).magnitude();
    if (radius < 1e-6)
      return items;

    MyVector dir = (p3 - p1);
    double dist = dir.magnitude();
    if (dist < 1e-6)
      dir = MyVector(1, 0);
    else
      dir = dir / dist;

    bool isOutside = dist > radius;
    MyVector q1 = p1 + dir * radius;
    MyVector q2 = p1 - dir * radius;

    double measurement = (m_data.dimensionType == DimensionType::Diametric)
                             ? radius * 2.0
                             : radius;
    QString prefix = (m_data.dimensionType == DimensionType::Diametric)
                         ? QString::fromUtf8("Ø")
                         : "R";

    MyVector lineStart, lineEnd, textBase;
    double arrowSize = style.arrowSize * finalScale;
    double extExtend = style.extExtend * finalScale;

    auto addArrow = [&](const MyVector &pos, const MyVector &direction) {
      MyVector n(-direction.y, direction.x);
      MyVector back = pos - direction * arrowSize;
      MyVector side1 = back + n * (arrowSize * 0.35);
      MyVector side2 = back - n * (arrowSize * 0.35);

      PolylineItem *arrow = new PolylineItem({pos, side1, side2}, true);
      LayerStyle ls = applyItemStyle(arrow, style.dimLineLayer);
      arrow->setBrush(QBrush(ls.color));
      items << arrow;
    };

    if (m_data.dimensionType == DimensionType::Diametric) {
      if (isOutside) {
        lineStart = q2 - dir * (arrowSize + extExtend);
        lineEnd = p3 + dir * extExtend;
        textBase = p3;
        addArrow(q1, dir * -1.0);
        addArrow(q2, dir);
      } else {
        lineStart = q2;
        lineEnd = q1;
        textBase = p1;
        addArrow(q1, dir);
        addArrow(q2, dir * -1.0);
      }
    } else { // Radial
      if (isOutside) {
        lineStart = p1;
        lineEnd = p3 + dir * extExtend;
        textBase = p3;
        addArrow(q1, dir * -1.0);
      } else {
        lineStart = p1;
        lineEnd = q1;
        textBase = (p1 + q1) * 0.5;
        addArrow(q1, dir);
      }
    }

    // 1. Main Line
    LineItem *dl = new LineItem(lineStart, lineEnd);
    applyItemStyle(dl, style.dimLineLayer);
    items << dl;

    // 3. Text
    QString txt = m_data.text;
    if (txt.isEmpty()) {
      txt = prefix + QString::number(measurement, 'f', style.precision);
    }

    double textGap = style.textGap * finalScale;
    MyVector textDir = dir;
    MyVector textNormal(-textDir.y, textDir.x);

    if (qAbs(textDir.y) > qAbs(textDir.x)) {
      if (textNormal.x > 1e-9) {
        textNormal = textNormal * -1.0;
        textDir = textDir * -1.0;
      }
    } else {
      if (textNormal.y < -1e-9) {
        textNormal = textNormal * -1.0;
        textDir = textDir * -1.0;
      }
    }
    double angleDeg = qRadiansToDegrees(qAtan2(textDir.y, textDir.x));
    MyVector textPos = textBase + textNormal * textGap;

    MyText textData;
    textData.text = txt;
    textData.pos = textPos;
    textData.rotation = angleDeg;
    textData.fontSize = style.textSize * finalScale;
    textData.fontFamily = "Arial";

    TextItem *ti = new TextItem(textData);
    LayerStyle tls = applyItemStyle(ti, style.dimTextLayer);
    ti->setPen(QPen(tls.color));
    items << ti;
  } else if (m_data.dimensionType == DimensionType::Angular) {
    // Similar to paint but creating items
    MyVector vertex = m_data.start;
    MyVector p1 = m_data.end;
    MyVector p2 = m_data.end2;
    MyVector pArc = m_data.dimLine;

    double radius = (pArc - vertex).magnitude();
    if (radius > 1e-6) {
      double ang1 = qRadiansToDegrees(qAtan2(p1.y - vertex.y, p1.x - vertex.x));
      double ang2 = qRadiansToDegrees(qAtan2(p2.y - vertex.y, p2.x - vertex.x));
      double angArc =
          qRadiansToDegrees(qAtan2(pArc.y - vertex.y, pArc.x - vertex.x));

      ang1 = MyMath::normalizeAngle(ang1);
      ang2 = MyMath::normalizeAngle(ang2);
      angArc = MyMath::normalizeAngle(angArc);

      double startAng = ang1;
      double span = ang2 - ang1;
      if (span < 0)
        span += 360.0;
      if (!MyMath::isAngleBetween(angArc, startAng, span)) {
        startAng = ang2;
        span = ang1 - ang2;
        if (span < 0)
          span += 360.0;
      }

      // 1. Arc Item
      ArcItem *ai = new ArcItem(MyArc(vertex, radius, startAng, span));
      applyItemStyle(ai, style.dimLineLayer);
      items << ai;

      // 2. Extension Lines
      double extOffset = style.extOffset * sceneScale;
      double extExtend = style.extExtend * sceneScale;

      auto addExt = [&](const MyVector &pt, double angle) {
        double rad = qDegreesToRadians(angle);
        MyVector dir(qCos(rad), qSin(rad));
        double dist = (pt - vertex).magnitude();
        if (radius > dist) {
          MyVector s = pt + dir * extOffset;
          MyVector e = vertex + dir * (radius + extExtend);
          LineItem *li = new LineItem(MyLine(s, e));
          applyItemStyle(li, style.dimLineLayer);
          items << li;
        }
      };

      if (!m_data.hideExt1)
        addExt(p1, ang1);
      if (!m_data.hideExt2)
        addExt(p2, ang2);

      // 2. Arrows (as polylines)
      double arrowSize = style.arrowSize * finalScale;
      auto addArrow = [&](double angle, bool clockwise) {
        double rad = qDegreesToRadians(angle);
        MyVector pos(vertex.x + qCos(rad) * radius,
                     vertex.y + qSin(rad) * radius);
        MyVector dir(qSin(rad), -qCos(rad));
        if (clockwise)
          dir = dir * -1.0;
        MyVector n(-dir.y, dir.x);
        MyVector back = pos - dir * arrowSize;
        MyVector side1 = back + n * (arrowSize * 0.35);
        MyVector side2 = back - n * (arrowSize * 0.35);

        PolylineItem *arrow = new PolylineItem({pos, side1, side2}, true);
        LayerStyle ls = applyItemStyle(arrow, style.dimLineLayer);
        arrow->setBrush(QBrush(ls.color));
        items << arrow;
      };
      addArrow(startAng, false);
      addArrow(startAng + span, true);

      // 3. Text Item
      QString txt = m_data.text;
      if (txt.isEmpty()) {
        txt = QString::number(span, 'f', style.precision) +
              QString::fromUtf8("°");
      }
      double midAng = startAng + span * 0.5;
      double textGap = style.textGap * finalScale;
      double radMid = qDegreesToRadians(midAng);
      MyVector textPos(vertex.x + qCos(radMid) * (radius + textGap),
                       vertex.y + qSin(radMid) * (radius + textGap));

      double rotation = midAng + 90.0;
      if (rotation > 90.0 && rotation < 270.0)
          rotation -= 180.0;

      MyText textData;
      textData.text = txt;
      textData.pos = textPos;
      textData.rotation = rotation;
      textData.fontSize = style.textSize * finalScale;
      textData.fontFamily = "Arial";

      TextItem *ti = new TextItem(textData);
      LayerStyle tls = applyItemStyle(ti, style.dimTextLayer);
      ti->setPen(QPen(tls.color));
      items << ti;
    }
  }

  return items;
}

QVariant DimensionItem::itemChange(GraphicsItemChange change,
                                   const QVariant &value) {
  if (change == QGraphicsItem::ItemSelectedChange && scene()) {
    if (!value.toBool()) {
      setGripsVisible(false);
    }
  }
  return CadItem::itemChange(change, value);
}

QPainterPath DimensionItem::shape() const {
  QPainterPath path = geometryPath();
  QPainterPathStroker stroker;
  stroker.setWidth(4.0); // 선택 감지 범위
  return stroker.createStroke(path);
}

QPainterPath DimensionItem::geometryPath() const {
  QPainterPath path;
  if (m_data.dimensionType == DimensionType::Radial ||
      m_data.dimensionType == DimensionType::Diametric) {
    MyVector p1 = m_data.start; // center
    MyVector p2 = m_data.end;   // point on circle
    MyVector p3 = m_data.dimLine;

    double radius = (p2 - p1).magnitude();
    MyVector dir = (p3 - p1);
    double dist = dir.magnitude();
    if (dist < 1e-6)
      dir = MyVector(1, 0);
    else
      dir = dir / dist;

    bool isOutside = dist > radius;
    MyVector q1 = p1 + dir * radius;
    MyVector q2 = p1 - dir * radius;
    MyGraphicsScene *scenePtr = qobject_cast<MyGraphicsScene *>(scene());
    double drawingScale = scenePtr ? scenePtr->drawingScale() : 1.0;
    DimensionStyle style =
        scenePtr ? scenePtr->dimensionStyle() : DimensionStyle();
    double extExtend = style.extExtend * drawingScale;

    MyVector lineStart, lineEnd;
    if (m_data.dimensionType == DimensionType::Diametric) {
      if (isOutside) {
        // geometryPath에서도 paint와 동일한 연장선 적용 (화살표 크기 고려)
        double arrowSize = 2.5 * drawingScale; // Use a reasonable default
        lineStart = q2 - dir * (arrowSize + extExtend);
        lineEnd = p3 + dir * extExtend;
      } else {
        lineStart = q2;
        lineEnd = q1;
      }
    } else { // Radial
      if (isOutside) {
        lineStart = p1;
        lineEnd = p3 + dir * extExtend;
      } else {
        lineStart = p1;
        lineEnd = q1;
      }
    }
    path.moveTo(lineStart.toPointF());
    path.lineTo(lineEnd.toPointF());
    return path;
  } else if (m_data.dimensionType == DimensionType::Angular) {
    MyVector vertex = m_data.start;
    MyVector p1 = m_data.end;
    MyVector p2 = m_data.end2;
    MyVector pArc = m_data.dimLine;

    double radius = (pArc - vertex).magnitude();
    if (radius < 1e-6)
      return path;

    double ang1 = qRadiansToDegrees(qAtan2(p1.y - vertex.y, p1.x - vertex.x));
    double ang2 = qRadiansToDegrees(qAtan2(p2.y - vertex.y, p2.x - vertex.x));
    double angArc =
        qRadiansToDegrees(qAtan2(pArc.y - vertex.y, pArc.x - vertex.x));

    ang1 = MyMath::normalizeAngle(ang1);
    ang2 = MyMath::normalizeAngle(ang2);
    angArc = MyMath::normalizeAngle(angArc);

    double startAng = ang1;
    double span = ang2 - ang1;
    if (span < 0)
      span += 360.0;
    if (!MyMath::isAngleBetween(angArc, startAng, span)) {
      startAng = ang2;
      span = 360.0 - span;
    }

    QRectF arcRect(vertex.x - radius, vertex.y - radius, radius * 2,
                   radius * 2);
    path.arcMoveTo(arcRect, -startAng);
    path.arcTo(arcRect, -startAng, -span);
    return path;
  }

  MyVector p1 = m_data.start;
  MyVector p2 = m_data.end;
  MyVector p3 = m_data.dimLine;

  MyVector v = p2 - p1;
  double len = v.magnitude();
  if (len < 1e-6)
    return path;

  MyVector dir;
  if (m_data.useFixedAngle) {
    double rad = qDegreesToRadians(m_data.fixedAngle);
    dir = MyVector(qCos(rad), qSin(rad));
  } else {
    dir = v / len;
  }
  MyVector normal(-dir.y, dir.x);
  double h = MyMath::dotProduct(p3 - p1, normal);

  MyVector d1 = p1 + normal * h;
  MyVector d2 =
      d1 +
      dir * (m_data.useFixedAngle ? MyMath::dotProduct(p2 - p1, dir) : len);

  path.moveTo(p1.toPointF());
  path.lineTo(d1.toPointF());
  path.lineTo(d2.toPointF());
  path.lineTo(p2.toPointF());

  return path;
}

QList<SnapPoint> DimensionItem::getSnapPoints() const {
  QList<SnapPoint> points;
  points << SnapPoint{m_data.start, SnapType::Endpoint};
  points << SnapPoint{m_data.end, SnapType::Endpoint};
  points << SnapPoint{m_data.dimLine, SnapType::Endpoint}; // Location grip
  if (m_data.dimensionType == DimensionType::Angular) {
    points << SnapPoint{m_data.end2, SnapType::Endpoint};
  }
  return points;
}

QList<QPointF> DimensionItem::getGripPoints() const {
  QList<QPointF> points;
  points << m_data.start.toPointF();
  points << m_data.end.toPointF();
  points << m_data.dimLine.toPointF();
  if (m_data.dimensionType == DimensionType::Angular) {
    points << m_data.end2.toPointF();
  }
  return points;
}

void DimensionItem::applyGripMove(int index, const MyVector &pos) {
  if (index == 0)
    m_data.start = pos;
  else if (index == 1)
    m_data.end = pos;
  else if (index == 2)
    m_data.dimLine = pos;
  else if (index == 3)
    m_data.end2 = pos;

  m_isBoundingRectValid = false;
  updateGrips();
  update();
}

QPainterPath DimensionItem::getGhostShape(int index,
                                          const MyVector &pos) const {
  DimensionItem ghost(m_data);
  ghost.applyGripMove(index, pos);
  return ghost.geometryPath();
}

double DimensionItem::distanceTo(const MyVector &pos) const {
  MyVector p1 = m_data.start;
  MyVector p2 = m_data.end;
  MyVector p3 = m_data.dimLine;

  MyVector v = p2 - p1;
  double len = v.magnitude();
  if (len < 1e-6)
    return pos.distanceTo(p1);

  MyVector dir;
  if (m_data.useFixedAngle) {
    double rad = qDegreesToRadians(m_data.fixedAngle);
    dir = MyVector(qCos(rad), qSin(rad));
  } else {
    dir = v / len;
  }
  MyVector normal(-dir.y, dir.x);
  double h = MyMath::dotProduct(p3 - p1, normal);

  MyVector d1 = p1 + normal * h;
  MyVector d2 =
      d1 +
      dir * (m_data.useFixedAngle ? MyMath::dotProduct(p2 - p1, dir) : len);

  auto distToSegment = [&](const MyVector &s, const MyVector &e,
                           const MyVector &p) {
    MyVector ab = e - s;
    MyVector ap = p - s;
    double l2 = MyMath::dotProduct(ab, ab);
    if (l2 < 1e-9)
      return p.distanceTo(s);
    double t = MyMath::dotProduct(ap, ab) / l2;
    if (t < 0.0)
      t = 0.0;
    else if (t > 1.0)
      t = 1.0;
    return p.distanceTo(s + ab * t);
  };

  double d = distToSegment(p1, d1, pos);
  d = qMin(d, distToSegment(p2, d2, pos));
  d = qMin(d, distToSegment(d1, d2, pos));

  if (m_data.dimensionType == DimensionType::Angular) {
    MyVector vertex = m_data.start;
    MyVector pArc = m_data.dimLine;
    double radius = (pArc - vertex).magnitude();
    if (radius > 1e-6) {
      double distToCenter = pos.distanceTo(vertex);
      d = qMin(d, std::abs(distToCenter - radius));
    }
  }

  return d;
}

SnapPoint DimensionItem::getNearestSnap(const MyVector &pos) const {
    Q_UNUSED(pos)
  return SnapPoint{m_data.start, SnapType::Endpoint};
}

void DimensionItem::move(const MyVector &delta) {
  m_data.start += delta;
  m_data.end += delta;
  m_data.dimLine += delta;
  m_data.end2 += delta;
  m_isBoundingRectValid = false;
  updateGrips();
  update();
}

void DimensionItem::mirror(const MyVector &p1, const MyVector &p2) {
  m_data.start = MyMath::reflectPoint(m_data.start, p1, p2);
  m_data.end = MyMath::reflectPoint(m_data.end, p1, p2);
  m_data.dimLine = MyMath::reflectPoint(m_data.dimLine, p1, p2);
  m_data.end2 = MyMath::reflectPoint(m_data.end2, p1, p2);
  m_isBoundingRectValid = false;
  updateGrips();
  update();
}

QPainterPath DimensionItem::getMovedPath(const MyVector &delta) const {
  DimensionItem ghost(m_data);
  ghost.move(delta);
  return ghost.geometryPath();
}

QPainterPath DimensionItem::getMirroredPath(const MyVector &p1,
                                            const MyVector &p2) const {
  DimensionItem ghost(m_data);
  ghost.mirror(p1, p2);
  return ghost.geometryPath();
}

void DimensionItem::rotate(const MyVector &base, double angle) {
  m_data.start = MyMath::rotatePoint(m_data.start, base, angle);
  m_data.end = MyMath::rotatePoint(m_data.end, base, angle);
  m_data.dimLine = MyMath::rotatePoint(m_data.dimLine, base, angle);
  m_data.end2 = MyMath::rotatePoint(m_data.end2, base, angle);
  m_isBoundingRectValid = false;
  updateGrips();
  update();
}

QPainterPath DimensionItem::getRotatedPath(const MyVector &base,
                                           double angle) const {
  DimensionItem ghost(m_data);
  ghost.rotate(base, angle);
  return ghost.geometryPath();
}

void DimensionItem::scale(const MyVector &base, double scaleFactor) {
  m_data.start = MyMath::scalePoint(m_data.start, base, scaleFactor);
  m_data.end = MyMath::scalePoint(m_data.end, base, scaleFactor);
  m_data.dimLine = MyMath::scalePoint(m_data.dimLine, base, scaleFactor);
  m_data.end2 = MyMath::scalePoint(m_data.end2, base, scaleFactor);
  m_isBoundingRectValid = false;
  updateGrips();
  update();
}

QPainterPath DimensionItem::getScaledPath(const MyVector &base,
                                          double scaleFactor) const {
  DimensionItem ghost(m_data);
  ghost.scale(base, scaleFactor);
  return ghost.geometryPath();
}

void DimensionItem::stretch(const QList<int> &indices, const MyVector &delta) {
  for (int idx : indices) {
    if (idx == 0)
      m_data.start += delta;
    else if (idx == 1)
      m_data.end += delta;
    else if (idx == 2)
      m_data.dimLine += delta;
    else if (idx == 3)
      m_data.end2 += delta;
  }
  m_isBoundingRectValid = false;
  updateGrips();
  update();
}

QPainterPath DimensionItem::getStretchedPath(const QList<int> &indices,
                                             const MyVector &delta) const {
  DimensionItem ghost(m_data);
  ghost.stretch(indices, delta);
  return ghost.geometryPath();
}

QMap<QString, QVariant> DimensionItem::getProperties() const {
  QMap<QString, QVariant> props;
  props["Type"] = "Dimension";
  props["Layer"] = m_layerName;

  MyGraphicsScene *scenePtr = qobject_cast<MyGraphicsScene *>(scene());
  DimensionStyle style =
      scenePtr ? scenePtr->dimensionStyle() : DimensionStyle();

  QString lineLayer =
      style.dimLineLayer.isEmpty() ? m_layerName : style.dimLineLayer;
  QString textLayer =
      style.dimTextLayer.isEmpty() ? m_layerName : style.dimTextLayer;

  props["DimLayer"] = lineLayer;
  props["TextLayer"] = textLayer;

  QString displayTxt = m_data.text;
  if (displayTxt.isEmpty()) {
    double measurement = 0.0;
    if (m_data.useFixedAngle) {
      double rad = qDegreesToRadians(m_data.fixedAngle);
      MyVector dir(qCos(rad), qSin(rad));
      measurement =
          std::abs(MyMath::dotProduct(m_data.end - m_data.start, dir));
    } else {
      measurement = (m_data.end - m_data.start).magnitude();
    }
    displayTxt = QString::number(measurement, 'f', style.precision);
  }
  props["Content"] = displayTxt;

  props["StartX"] = m_data.start.x;
  props["StartY"] = m_data.start.y;
  props["EndX"] = m_data.end.x;
  props["EndY"] = m_data.end.y;
  props["DimLineX"] = m_data.dimLine.x;
  props["DimLineY"] = m_data.dimLine.y;
  props["HideExt1"] = m_data.hideExt1;
  props["HideExt2"] = m_data.hideExt2;
  props["UseFixedAngle"] = m_data.useFixedAngle;
  props["FixedAngle"] = m_data.fixedAngle;

  QString typeStr = "Linear";
  if (m_data.dimensionType == DimensionType::Radial)
    typeStr = "Radial";
  else if (m_data.dimensionType == DimensionType::Diametric)
    typeStr = "Diametric";
  else if (m_data.dimensionType == DimensionType::Angular)
    typeStr = "Angular";
  props["DimType"] = typeStr;

  return props;
}

void DimensionItem::setGripsVisible(bool visible) {
  if (m_startGrip)
    m_startGrip->setVisible(visible);
  if (m_endGrip)
    m_endGrip->setVisible(visible);
  if (m_dimLineGrip)
    m_dimLineGrip->setVisible(visible);
  if (m_end2Grip)
    m_end2Grip->setVisible(visible);
}

bool DimensionItem::setProperty(const QString &key, const QVariant &value) {
  if (key == "Content") {
    m_data.text = value.toString();
    update();
    return true;
  } else if (key == "HideExt1") {
    m_data.hideExt1 = value.toBool();
    update();
    return true;
  } else if (key == "HideExt2") {
    m_data.hideExt2 = value.toBool();
    update();
    return true;
  } else if (key == "UseFixedAngle") {
    m_data.useFixedAngle = value.toBool();
    update();
    return true;
  } else if (key == "FixedAngle") {
    m_data.fixedAngle = value.toDouble();
    update();
    return true;
  } else if (key == "DimType") {
    QString val = value.toString();
    if (val == "Linear")
      m_data.dimensionType = DimensionType::Linear;
    else if (val == "Radial")
      m_data.dimensionType = DimensionType::Radial;
    else if (val == "Diametric")
      m_data.dimensionType = DimensionType::Diametric;
    else if (val == "Angular")
      m_data.dimensionType = DimensionType::Angular;
    update();
    return true;
  }
  return false;
}

void DimensionItem::updateGrips() {
  if (!m_startGrip)
    m_startGrip = new GripItem(0, this);
  if (!m_endGrip)
    m_endGrip = new GripItem(1, this);
  if (!m_dimLineGrip)
    m_dimLineGrip = new GripItem(2, this);

  m_startGrip->updatePosition(m_data.start.toPointF());
  m_endGrip->updatePosition(m_data.end.toPointF());
  m_dimLineGrip->updatePosition(m_data.dimLine.toPointF());

  if (m_data.dimensionType == DimensionType::Angular) {
    if (!m_end2Grip)
      m_end2Grip = new GripItem(3, this);
    m_end2Grip->updatePosition(m_data.end2.toPointF());
    m_end2Grip->setVisible(m_startGrip->isVisible());
  } else if (m_end2Grip) {
    m_end2Grip->setVisible(false);
  }
}
