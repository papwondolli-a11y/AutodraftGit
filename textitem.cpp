#include "textitem.h"
#include "cadutils.h"
#include "mymath.h"
#include <QDebug>
#include <QFontMetricsF>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QTransform>

TextItem::TextItem(const MyText &data, QGraphicsItem *parent)
    : CadItem(parent), m_data(data) {
    m_font = QFont(m_data.fontFamily);
    m_font.setPointSizeF(m_data.fontSize);
    applyFontStyle();
    setPos(m_data.pos.toPointF());

    QTransform t;
    t.scale(1, -1);
    t.rotate(-m_data.rotation);
    setTransform(t);
    updateCachedShape();
}

void TextItem::applyFontStyle() {
    m_font.setBold(m_data.bold);
    m_font.setItalic(m_data.italic);
    // 밑줄은 addText() 가 그려주지 않으므로 직접 그린다(underlinePath). 폰트 플래그는
    // 일반 QFont 사용처(메트릭 등)와 일관되도록 같이 켜 둔다.
    m_font.setUnderline(m_data.underline);
}

QPainterPath TextItem::underlinePath() const {
    QPainterPath path;
    if (!m_data.underline)
        return path;
    QFontMetricsF fm(m_font);
    const QStringList lines = m_data.text.split('\n');
    const double lineSpacing = fm.lineSpacing();
    const double thickness = qMax(fm.lineWidth(), m_data.fontSize / 18.0);
    for (int i = 0; i < lines.size(); ++i) {
        const double w = fm.horizontalAdvance(lines[i]);
        if (w <= 0.0)
            continue;
        double xOff = 0.0;
        if (m_data.alignH == 1)      xOff = -w / 2.0;
        else if (m_data.alignH == 2) xOff = -w;
        const double y = i * lineSpacing + fm.underlinePos();
        path.addRect(QRectF(xOff, y, w, thickness));
    }
    return path;
}

CadItem *TextItem::clone() const {
    auto *item = new TextItem(m_data);
    item->setLayerName(layerName());
    item->setPen(pen());
    return item;
}

QRectF TextItem::boundingRect() const {
    QFontMetricsF fm(m_font);

    QStringList lines = m_data.text.split('\n');
    double maxWidth = 0.0;

    for (int i = 0; i < lines.size(); ++i) {
        double w = fm.horizontalAdvance(lines[i]);
        if (w > maxWidth) {
            maxWidth = w;
        }
    }

    double lineSpacing = fm.lineSpacing();
    double totalHeight = lineSpacing * lines.size();

    // x-offset based on alignment: left=0, center=-w/2, right=-w
    double xOff = 0.0;
    if (m_data.alignH == 1) xOff = -maxWidth / 2.0;
    else if (m_data.alignH == 2) xOff = -maxWidth;

    return QRectF(xOff, -fm.ascent(), maxWidth, totalHeight);
}

void TextItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
                     QWidget *widget) {
    Q_UNUSED(widget);

    // 1. 하이라이트 배경 그리기
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

    // 2. 텍스트 그리기 (사각형 영역(QRectF) 대신 베이스라인(QPointF) 기준)
    painter->setFont(m_font);
    
    QPainterPath path;
    QFontMetricsF fm(m_font);
    QStringList lines = m_data.text.split('\n');
    double lineSpacing = fm.lineSpacing();

    for (int i = 0; i < lines.size(); ++i) {
        double xOff = 0.0;
        if (m_data.alignH == 1)      xOff = -fm.horizontalAdvance(lines[i]) / 2.0;
        else if (m_data.alignH == 2) xOff = -fm.horizontalAdvance(lines[i]);
        path.addText(QPointF(xOff, i * lineSpacing), m_font, lines[i]);
    }

    painter->fillPath(path, m_pen.color());
    if (m_data.underline)
        painter->fillPath(underlinePath(), m_pen.color());
}

QPainterPath TextItem::geometryPath() const {
    // QPainterPath path;
    // path.addRect(boundingRect());
    // return path;
    return m_cachedGeometry;
}

void TextItem::updateCachedShape() {
    QPainterPath textPath = textOutlinePath();

    QPainterPathStroker stroker;
    stroker.setWidth(2.0); // 클릭 마진

    m_cachedShape = stroker.createStroke(textPath).united(textPath);
    m_cachedGeometry = textPath;
}

QPainterPath TextItem::shape() const {
    return m_cachedShape;
}

QList<SnapPoint> TextItem::getSnapPoints() const {
    QList<SnapPoint> points;
    points.append(SnapPoint(m_data.pos, SnapType::Endpoint));
    return points;
}

QList<QPointF> TextItem::getGripPoints() const {
    QList<QPointF> points;
    points.append(m_data.pos.toPointF());
    return points;
}

void TextItem::applyGripMove(int index, const MyVector &pos) {
    if (index == 0) {
        prepareGeometryChange();
        m_data.pos = pos;
        setPos(m_data.pos.toPointF());
        update();
    }
}

QPainterPath TextItem::getGhostShape(int index, const MyVector &pos) const {
    if (index == 0) {
        QTransform t;
        t.translate(pos.x, pos.y);
        t *= this->transform();
        return t.map(geometryPath());
    }
    return QPainterPath();
}

double TextItem::distanceTo(const MyVector &pos) const {
    QPointF localPos = mapFromScene(pos.toPointF());
    if (boundingRect().contains(localPos)) {
        return 0.0;
    }
    return (m_data.pos - pos).magnitude();
}

SnapPoint TextItem::getNearestSnap(const MyVector &pos) const {
    Q_UNUSED(pos)
    return SnapPoint(m_data.pos, SnapType::Endpoint);
}

void TextItem::move(const MyVector &delta) {
    prepareGeometryChange();
    m_data.pos = m_data.pos + delta;
    setPos(m_data.pos.toPointF());
    update();
}

void TextItem::mirror(const MyVector &p1, const MyVector &p2) {
    prepareGeometryChange();
    m_data.pos = MyMath::reflectPoint(m_data.pos, p1, p2);
    setPos(m_data.pos.toPointF());
    update();
}

QPainterPath TextItem::textOutlinePath() const {
    QPainterPath path;
    QFontMetricsF fm(m_font);
    QStringList lines = m_data.text.split('\n');
    double lineSpacing = fm.lineSpacing();

    for (int i = 0; i < lines.size(); ++i) {
        double xOff = 0.0;
        if (m_data.alignH == 1)      xOff = -fm.horizontalAdvance(lines[i]) / 2.0;
        else if (m_data.alignH == 2) xOff = -fm.horizontalAdvance(lines[i]);
        path.addText(xOff, i * lineSpacing, m_font, lines[i]);
    }
    if (m_data.underline)
        path.addPath(underlinePath());
    return path;
}

QPainterPath TextItem::getMovedPath(const MyVector &delta) const {
    QTransform t;
    MyVector newPos = m_data.pos + delta;
    t.translate(newPos.x, newPos.y);
    t.scale(1, -1);
    t.rotate(-m_data.rotation);
    return t.map(textOutlinePath());
}

QPainterPath TextItem::getMirroredPath(const MyVector &p1,
                                       const MyVector &p2) const {
    MyVector reflectedPos = MyMath::reflectPoint(m_data.pos, p1, p2);
    QTransform t;
    t.translate(reflectedPos.x, reflectedPos.y);
    t.scale(1, -1);
    t.rotate(-m_data.rotation);
    return t.map(textOutlinePath());
}

QMap<QString, QVariant> TextItem::getProperties() const {
    QMap<QString, QVariant> props;
    props.insert("Type", "Text");
    props.insert("Layer", layerName());
    props.insert("PosX", pos().x());
    props.insert("PosY", pos().y());
    props.insert("Rotation", m_data.rotation);
    props.insert("Content", m_data.text);
    props.insert("FontFamily", m_data.fontFamily);
    props.insert("FontSize", m_data.fontSize);
    props.insert("FontBold", m_data.bold);
    props.insert("FontItalic", m_data.italic);
    props.insert("FontUnderline", m_data.underline);
    props.insert("isAttribute", m_data.isAttribute);
    props.insert("tag", m_data.tag);
    props.insert("prompt", m_data.prompt);
    return props;
}

void TextItem::setGripsVisible(bool visible) {
    Q_UNUSED(visible);
}

QVariant TextItem::itemChange(GraphicsItemChange change,
                              const QVariant &value) {
    if (change == QGraphicsItem::ItemSelectedChange) {
        if (!value.toBool()) {
            setGripsVisible(false);
        }
    }
    return CadItem::itemChange(change, value);
}

bool TextItem::setProperty(const QString &key, const QVariant &value) {
    prepareGeometryChange();
    bool isGeometryChanged = false;

    if (key == "Content") {
        m_data.text = CadUtils::processText(value.toString());
        isGeometryChanged = true;
    } else if (key == "FontFamily") {
        m_data.fontFamily = value.toString();
        m_font.setFamily(m_data.fontFamily);
        isGeometryChanged = true;
    } else if (key == "FontSize") {
        m_data.fontSize = value.toDouble();
        m_font.setPointSizeF(m_data.fontSize);
        isGeometryChanged = true;
    } else if (key == "FontBold") {
        m_data.bold = value.toBool();
        applyFontStyle();
        isGeometryChanged = true;
    } else if (key == "FontItalic") {
        m_data.italic = value.toBool();
        applyFontStyle();
        isGeometryChanged = true;
    } else if (key == "FontUnderline") {
        m_data.underline = value.toBool();
        applyFontStyle();
        isGeometryChanged = true;
    } else if (key == "isAttribute") {
        m_data.isAttribute = value.toBool();
    } else if (key == "tag") {
        m_data.tag = value.toString();
    } else if (key == "prompt") {
        m_data.prompt = value.toString();
    } else {
        return false;
    }

    // [핵심] 텍스트 내용이나 폰트가 변경되었을 때만 캐시를 다시 계산합니다.
    if (isGeometryChanged) {
        updateCachedShape();
    }

    update();
    return true;
}

void TextItem::setTextData(const MyText &data) {
    prepareGeometryChange();
    m_data = data;
    m_data.text = CadUtils::processText(m_data.text);
    m_font = QFont(m_data.fontFamily);
    m_font.setPointSizeF(m_data.fontSize);
    applyFontStyle();
    setPos(m_data.pos.toPointF());

    QTransform t;
    t.scale(1, -1);
    t.rotate(-m_data.rotation);
    setTransform(t);
    //  전체 데이터가 덮어씌워졌으므로 캐시를 갱신합니다.
    updateCachedShape();
    update();
}

void TextItem::rotate(const MyVector &base, double angle) {
    prepareGeometryChange();
    m_data.pos = MyMath::rotatePoint(m_data.pos, base, angle);
    m_data.rotation = MyMath::normalizeAngle(m_data.rotation + angle);

    setPos(m_data.pos.toPointF());

    QTransform t;
    t.scale(1, -1);
    t.rotate(-m_data.rotation);
    setTransform(t);

    update();
}

QPainterPath TextItem::getRotatedPath(const MyVector &base,
                                      double angle) const {
    MyVector rPos = MyMath::rotatePoint(m_data.pos, base, angle);
    double rAngle = MyMath::normalizeAngle(m_data.rotation + angle);

    QTransform t;
    t.translate(rPos.x, rPos.y);
    t.scale(1, -1);
    t.rotate(-rAngle);
    return t.map(textOutlinePath());
}

void TextItem::scale(const MyVector &base, double scaleFactor) {
    prepareGeometryChange();
    m_data.pos = MyMath::scalePoint(m_data.pos, base, scaleFactor);

    m_data.fontSize = m_data.fontSize * std::abs(scaleFactor);
    if (m_data.fontSize < 0.1)
        m_data.fontSize = 0.1;

    m_font.setPointSizeF(m_data.fontSize);
    setPos(m_data.pos.toPointF());

    QTransform t;
    t.scale(1, -1);
    t.rotate(-m_data.rotation);
    setTransform(t);
    updateCachedShape();
    update();
}

QPainterPath TextItem::getScaledPath(const MyVector &base,
                                     double scaleFactor) const {
    MyVector sPos = MyMath::scalePoint(m_data.pos, base, scaleFactor);

    double sFontSize = m_data.fontSize * std::abs(scaleFactor);
    if (sFontSize < 0.1)
        sFontSize = 0.1;

    QFont tempFont = m_font;
    tempFont.setPointSizeF(sFontSize);

    QPainterPath localPath;
    QFontMetricsF fm(tempFont);
    QStringList lines = m_data.text.split('\n');
    double lineSpacing = fm.lineSpacing();

    for (int i = 0; i < lines.size(); ++i) {
        localPath.addText(0.0, i * lineSpacing, tempFont, lines[i]);
    }

    QTransform t;
    t.translate(sPos.x, sPos.y);
    t.scale(1, -1);
    t.rotate(-m_data.rotation);
    return t.map(localPath);
}

void TextItem::stretch(const QList<int> &indices, const MyVector &delta) {
    prepareGeometryChange();
    // 인덱스 루프를 돌며 stretch 적용
    for (int i = 0; i < indices.size(); ++i) {
        if (indices[i] == 0) {
            m_data.pos = m_data.pos + delta;
            setPos(m_data.pos.toPointF());
            break;
        }
    }
    update();
}

QPainterPath TextItem::getStretchedPath(const QList<int> &indices,
                                        const MyVector &delta) const {
    for (int i = 0; i < indices.size(); ++i) {
        if (indices[i] == 0) {
            return getMovedPath(delta);
        }
    }
    return QPainterPath();
}