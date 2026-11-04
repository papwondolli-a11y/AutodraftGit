#ifndef TEXTITEM_H
#define TEXTITEM_H

#include "caditem.h"
#include "mytext.h"
#include <QFont>

class TextItem final : public CadItem {
public:
    TextItem(const MyText &data, QGraphicsItem *parent = nullptr);
    ~TextItem() override = default;

    CadItem *clone() const override;

    // QGraphicsItem 필수 구현
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option,
               QWidget *widget) override;
    QPainterPath shape() const override;
    QPainterPath geometryPath() const override;

    // CadItem 필수 구현
    QList<SnapPoint> getSnapPoints() const override;
    QList<QPointF> getGripPoints() const override;
    QPainterPath getGhostShape(int index, const MyVector &pos) const override;
    SnapPoint getNearestSnap(const MyVector &pos) const override;
    const MyEntity *getEntityData() const override { return &m_data; }

    QPainterPath getMovedPath(const MyVector &delta) const override;
    QPainterPath getMirroredPath(const MyVector &p1,
                                 const MyVector &p2) const override;
    QPainterPath getRotatedPath(const MyVector &base,
                                double angle) const override;
    QPainterPath getScaledPath(const MyVector &base,
                               double scaleFactor) const override;
    QPainterPath getStretchedPath(const QList<int> &indices,
                                  const MyVector &delta) const override;

    QMap<QString, QVariant> getProperties() const override;

    void applyGripMove(int index, const MyVector &pos) override;
    double distanceTo(const MyVector &pos) const override;
    void move(const MyVector &delta) override;
    void mirror(const MyVector &p1, const MyVector &p2) override;
    void rotate(const MyVector &base, double angle) override;
    void scale(const MyVector &base, double scaleFactor) override;
    void stretch(const QList<int> &indices, const MyVector &delta) override;

    void setGripsVisible(bool visible) override;
    bool setProperty(const QString &key, const QVariant &value) override;

protected:
    QVariant itemChange(GraphicsItemChange change,
                        const QVariant &value) override;

    // 데이터 접근
    void setTextData(const MyText &data);
    MyText getTextData() const { return m_data; }

private:
    QPainterPath textOutlinePath() const;
    QPainterPath underlinePath() const;
    void applyFontStyle();

    MyText m_data;
    QFont m_font;
    // 형상이 바뀔 때만 호출하여 캐시를 갱신할 내부 함수
    void updateCachedShape();

    // 계산된 형상을 저장해둘 변수 (성능 최적화용)
    QPainterPath m_cachedShape;
    QPainterPath m_cachedGeometry;
};

#endif // TEXTITEM_H