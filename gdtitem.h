#ifndef GDTITEM_H
#define GDTITEM_H

#include "caditem.h"
#include "mygdt.h"
#include <QtMath>

class LineItem;
class TextItem;

class GdtItem final : public CadItem {
public:
    explicit GdtItem(const GdtData &data, QGraphicsItem *parent = nullptr);

    CadItem *clone() const override;

    QRectF boundingRect() const override;
    void  paint(QPainter *painter,
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
    const MyEntity *getEntityData() const override { return &m_data; }

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

    const GdtData &gdtData() const { return m_data; }

    // [신규] 씬에 추가되기 전 고스트 미리보기를 위한 강제 스케일 주입
    void setOverrideScale(double scale) { m_overrideScale = scale; }

    // [신규] 외부 파싱된 지시선을 수동으로 연결
    // leaderPoints[0] = 화살표 위치, leaderPoints.last() = 프레임 기준점
    // 마지막 선분 방향으로 rotation 갱신해야 export 시 isFlipped 오프셋이 정확히 적용됨
    // updatePosition이 false이면 DXF 원본 위치를 보존 (import 시 사용)
    void setLeaderPoints(const QList<MyVector>& pts, bool updatePosition = true) {
        m_data.leaderPoints = pts;
        m_data.useLeader = (pts.size() >= 2);
        if (pts.size() >= 2 && updatePosition) {
            m_data.position = pts.last();
            MyVector dir = pts.last() - pts[pts.size() - 2];
            if (dir.magnitude() > 1e-6) {
                m_data.rotation = qRadiansToDegrees(qAtan2(dir.y, dir.x));
            }
        }
        prepareGeometryChange();
        update();
    }

    // DXF 내보내기에서 박스 교차점을 계산하기 위해 public으로 공개
    QList<MyVector> getAdjustedLeaderPoints() const;

// protected:
//     // 심볼을 그리는 함수 (DPI 보정 제거)
//     void drawSymbol(QPainter *painter, const QRectF &cell, GdtSymbol sym) const;

private:
    double getScale() const; // [신규] 도면 스케일 획득 헬퍼

    double cellHeight() const;
    double symbolCellWidth() const;
    double toleranceCellWidth() const;
    double datumCellWidth() const;
    double totalWidth() const;

    void drawSymbol(QPainter *painter, const QRectF &cell, GdtSymbol sym) const;

    QRectF localFrameRect() const;
    QTransform frameTransform() const;

    double getDrawRotation() const;

    GdtData m_data;
    double m_overrideScale = -1.0; // 기본값 -1 (사용 안 함)
};

#endif // GDTITEM_H