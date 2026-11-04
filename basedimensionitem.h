#ifndef BASEDIMENSIONITEM_H
#define BASEDIMENSIONITEM_H

#include "caditem.h"
#include "mydimension.h"
#include "gripitem.h"
#include "unitconvert.h"

class BaseDimensionItem : public CadItem {
public:
    explicit BaseDimensionItem(const MyDimension &data, QGraphicsItem *parent = nullptr);
    ~BaseDimensionItem() override;

    // 공통적으로 구현 가능한 CadItem 가상 함수들
    QRectF boundingRect() const override;
    QList<SnapPoint> getSnapPoints() const override;
    QList<QPointF> getGripPoints() const override;
    void applyGripMove(int index, const MyVector &pos) override;
    SnapPoint getNearestSnap(const MyVector &pos) const override;
    const MyEntity *getEntityData() const override { return &m_data; }

    void move(const MyVector &delta) override;
    void mirror(const MyVector &p1, const MyVector &p2) override;
    void rotate(const MyVector &base, double angle) override;
    void scale(const MyVector &base, double scaleFactor) override;
    void stretch(const QList<int> &indices, const MyVector &delta) override;

    // 본문 숫자값을 치수선 geometry에 적용 (각 타입에 맞게 end/end2 좌표 실제 갱신)
    bool applyMeasurementValue(double newValue);

    // 치수 스타일 변경(setDimensionStyle) 시 외부에서 캐시를 무효화하는 메서드
    void invalidateBoundingRect() {
        m_isBoundingRectValid = false;
        prepareGeometryChange();
    }

    void setGripsVisible(bool visible) override;
    bool gripsEnabled() const override { return false; }
    bool setProperty(const QString &key, const QVariant &value) override;
    QMap<QString, QVariant> getProperties() const override;

    const MyDimension &data() const { return m_data; }

    // =========================================================================
    // [신규 추가] 화살표 숨김/표시 명시적 제어 함수
    // =========================================================================
    void setArrow1Hidden(bool hidden);
    bool isArrow1Hidden() const;
    void setArrow2Hidden(bool hidden);
    bool isArrow2Hidden() const;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void updateGrips();

    // 렌더링 공통 도우미 함수들
    void drawArrow(QPainter *painter, const MyVector &pos, const MyVector &direction, double arrowSize, const QColor &color,
                   ArrowheadStyle headStyle = ArrowheadStyle::Filled) const;
    void drawSelectionHighlight(QPainter *painter, const QStyleOptionGraphicsItem *option) const;
    DimensionStyle getDimStyle(double &outScale) const;
    void setupDimPen(QPainter *painter, const DimensionStyle &style, double viewScale) const;

    // [Phase 5] 도면 길이 단위(mm/inch) — 씬이 없으면 Mm.
    LinearUnit sceneLinearUnit() const;
    // 길이형 치수 "표시 숫자"에 곱할 단위 배율. mm→1.0, inch→1/25.4.
    //   각도 치수(DimensionType::Angular)는 항상 1.0. 화살표/보조선 등 기하에는
    //   적용하지 않는다 — measStr / 공차 / 한계치 문자열에만 곱한다.
    double unitDisplayFactor() const;
    // 길이형 치수 텍스트 뒤에 붙일 단위 기호. mm→"", inch→"\"". 각도 치수는 "".
    QString unitLengthSuffix() const;

    void calculateTextLayout(double measurement, const QString &baseValueText, const DimensionStyle &style, double finalScale,
                             QString &outMain, QString &outUpper, QString &outLower,
                             double &outMainWidth, double &outTolWidth, double &outTotalWidth) const;

    void drawDimensionText(QPainter *painter, double measurement, const QString &baseValueText,
                           const MyVector &pos, double angle, const DimensionStyle &style, double finalScale) const;

    QPainterPath createTextBoundingPath(double measurement, const QString& baseValueText,
                                        const MyVector& pos, double angle) const;

    MyDimension m_data;

    GripItem *m_startGrip = nullptr;
    GripItem *m_endGrip = nullptr;
    GripItem *m_dimLineGrip = nullptr;
    GripItem *m_end2Grip = nullptr;
    GripItem *m_textGrip = nullptr;

    mutable QRectF m_cachedBoundingRect;
    mutable bool m_isBoundingRectValid = false;
};

#endif // BASEDIMENSIONITEM_H