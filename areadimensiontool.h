#ifndef AREADIMENSIONTOOL_H
#define AREADIMENSIONTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include "snaptypes.h"
#include <QList>
#include <QPointF>

// 영역치수 (Area Dimension) 도구
// 사용자가 지정한 사각 영역(P1, P2) 안에서 단(step)이 있는 형상을 찾아
// 각 단마다 지름(Ø) 치수를 자동 생성하고, P3 위치를 기준으로 바깥쪽으로
// 치수선 설정 매니저의 간격(baselineSpacing)만큼 스택하여 배치한다.
//
// 단이 가로로(X축) 배열되어 세로 치수선이 필요한 형상인지, 세로로(Y축)
// 배열되어 가로 치수선이 필요한 형상인지는 P1~P2 영역만으로는 알 수 없으므로
// (영역 사각형의 가로/세로 비율이 실제 단 배열 방향과 무관할 수 있음),
// 영역 안의 실제 선분을 양쪽 방향으로 모두 분석해 더 많은/큰 단이 검출되는
// 쪽을 채택한다.
class AreaDimensionTool : public AbstractTool {
    Q_OBJECT
public:
    explicit AreaDimensionTool(QObject *parent = nullptr);
    ~AreaDimensionTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;

    bool processCommand(const QString &command) override;

protected slots:
    void onFinish() override;
    void onCancel() override;

private:
    // 축에 정렬된 선분 하나. 세로선이면 pos=x, [lo,hi]=y범위.
    // 가로선이면 pos=y, [lo,hi]=x범위.
    struct AxisSeg {
        double pos;
        double lo;
        double hi;
    };

    // 영역 안에서 검출된 하나의 "단(step)" 구간.
    // 세로 치수 모드: boundLo/Hi = X 범위(단의 폭), measLo/Hi = Y 범위(치수값)
    // 가로 치수 모드: boundLo/Hi = Y 범위(단의 폭), measLo/Hi = X 범위(치수값)
    struct StepSlab {
        double boundLo;
        double boundHi;
        double measLo;
        double measHi;
        double span() const { return measHi - measLo; }
    };

    enum State {
        WaitSymbolOption,
        WaitP1,
        WaitP2,
        WaitP3
    };

    void cancelDrawing();
    QPointF resolvePoint(const QPointF &pos, SnapType *outType = nullptr) const;
    void computeSlabs();
    QList<StepSlab> buildSlabsFromAxis(const QList<AxisSeg> &boundarySegs,
                                       const QList<AxisSeg> &measureSegs,
                                       double regionBoundLo,
                                       double regionBoundHi) const;
    void finalizeDimensions(const QPointF &p3Pos);
    void updateGhostPreview();
    void buildDimPreview(const QPointF &p3Pos, QPainterPath &outPath) const;

    State m_state;
    bool m_isDrawing;
    bool m_useDiameterSymbol;
    bool m_verticalDims; // true: 세로 치수선(단이 X축 배열), false: 가로 치수선(단이 Y축 배열)

    MyVector m_p1;
    MyVector m_p2;
    QPointF m_currentMousePos;

    QList<StepSlab> m_slabs;
};

#endif // AREADIMENSIONTOOL_H
