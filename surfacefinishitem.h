#ifndef SURFACEFINISHITEM_H
#define SURFACEFINISHITEM_H

#include "caditem.h"
#include "mysurfacefinish.h"
#include <QtMath>

class SurfaceFinishItem final : public CadItem {
public:
    explicit SurfaceFinishItem(const SurfaceFinishData &data, QGraphicsItem *parent = nullptr);

    CadItem *clone() const override;

    QRectF boundingRect() const override;
    void  paint(QPainter *painter,
                const QStyleOptionGraphicsItem *option,
                QWidget *widget) override;
    QPainterPath  shape() const override;
    QPainterPath  geometryPath() const override;

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

    const SurfaceFinishData &surfaceFinishData() const { return m_data; }

    // 씬에 추가되기 전 고스트 미리보기를 위한 강제 스케일 주입
    void setOverrideScale(double scale) { m_overrideScale = scale; }
    // 씬에 추가되기 전에는 dimensionStyle()에 접근할 수 없으므로,
    // 고스트 미리보기가 실제 배치 결과와 같은 최소 크기를 갖도록 강제 주입
    void setOverrideArrowSize(double arrowSize) { m_overrideArrowSize = arrowSize; }

    // DXF 내보내기 등 외부에서 지시선 끝점(꼭짓점) 보정값을 얻기 위해 공개
    QList<MyVector> getAdjustedLeaderPoints() const;

private:
    double getScale() const;
    double unitSize() const; // textHeight * scale 기반 기본 단위
    double effectiveArrowSize() const; // 씬이 없는 임시(고스트) 복사본에도 전달할 화살표 크기

    bool   isFlipped() const;      // 회전각이 90~270도 구간이면 좌우 반전
    double getDrawRotation() const;
    QString displayLine1() const; // Ra 값(또는 구기호 등급명) 표시 문자열

    // 로컬(회전 전) 좌표계 기준 주요 지점들
    QPointF vertexPoint() const;      // (0,0)
    QPointF shortLegPoint() const;
    QPointF longLegPoint() const;
    QPointF barEndPoint() const;      // MachiningRequired 막대 끝점
    QPointF circleCenter() const;     // MachiningProhibited 원 중심
    double  circleRadius() const;
    QPointF triangleClosePoint() const; // TriangleFlag: 짧은 다리 끝(p1)과 같은 높이에서
                                         // 삼각형을 닫고 긴 다리가 시작되는 점(p4)
    QPointF textAnchorPoint() const;  // 값 텍스트 좌측 기준점

    QTransform frameTransform() const;
    QPainterPath localGlyphPath() const; // 회전 전 로컬 좌표계의 기호(체크마크/막대/원/삼각형) 외곽선

    SurfaceFinishData m_data;
    double m_overrideScale = -1.0;
    double m_overrideArrowSize = -1.0;
};

#endif // SURFACEFINISHITEM_H
