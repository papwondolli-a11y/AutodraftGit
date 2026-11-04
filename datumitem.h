#ifndef DATUMITEM_H
#define DATUMITEM_H

#include "caditem.h"

struct DatumData {
    QString text;           // 추출된 데이텀 문자 (A, B, C 등)
    MyVector startPoint;    // 삼각형 꼭짓점 (지시 시작점)
    MyVector endPoint;      // 데이텀 박스 중심점 (끝점)
    double textHeight;      // 기준 문자 높이
    QList<MyVector> leaderPoints; // [신규] 다중 지시선 정점들
    QString fontName;       // [신규] 폰트 이름

    DatumData() : textHeight(2.5), fontName("Arial") {}
};

class DatumItem final : public CadItem {
public:
    explicit DatumItem(const DatumData &data, QGraphicsItem *parent = nullptr);

    CadItem *clone() const override;

    QRectF        boundingRect() const override;
    void          paint(QPainter *painter,
    const QStyleOptionGraphicsItem *option,
    QWidget *widget) override;
    QPainterPath  shape() const override;
    QPainterPath  geometryPath() const override;
    QList<CadItem*> explode() const override;

    QList<SnapPoint>  getSnapPoints() const override;
    QList<QPointF>    getGripPoints() const override;
    void              applyGripMove(int index, const MyVector &pos) override;
    QPainterPath      getGhostShape(int index, const MyVector &pos) const override;

    double       distanceTo(const MyVector &pos) const override;
    SnapPoint    getNearestSnap(const MyVector &pos) const override;
    const MyEntity *getEntityData() const override { return nullptr; }
    //  FileManager의 Save를 위한 데이터 접근자
    const DatumData &datumData() const { return m_data; }

    void move(const MyVector &delta) override;
    void mirror(const MyVector &p1, const MyVector &p2) override;
    void rotate(const MyVector &base, double angle) override;
    void scale(const MyVector &base, double scaleFactor) override;
    void stretch(const QList<int> &indices, const MyVector &delta) override;

    QPainterPath getMovedPath(const MyVector &delta) const override;
    QPainterPath getMirroredPath(const MyVector &p1, const MyVector &p2) const override;
    QPainterPath getRotatedPath(const MyVector &base, double angle) const override;
    QPainterPath getScaledPath(const MyVector &base, double scaleFactor) const override;
    QPainterPath getStretchedPath(const QList<int> &indices, const MyVector &delta) const override;

    QMap<QString, QVariant> getProperties() const override;
    bool setProperty(const QString &key, const QVariant &value) override;
    void setGripsVisible(bool visible) override;

    // [신규] 외부 파싱된 지시선을 수동으로 연결
    void setLeaderPoints(const QList<MyVector>& pts, bool updatePosition = true) {
        if (pts.isEmpty()) return;
        m_data.leaderPoints = pts;
        if (updatePosition) {
            m_data.startPoint = pts.first();
            m_data.endPoint = pts.last();
        }
        prepareGeometryChange();
        update();
    }

    void setOverrideScale(double scale) { m_overrideScale = scale; }

private:
    double getScale() const;

    DatumData m_data;
    double m_overrideScale = -1.0;
};

#endif // DATUMITEM_H