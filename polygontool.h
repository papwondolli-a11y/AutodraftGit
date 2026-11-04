#ifndef POLYGONTOOL_H
#define POLYGONTOOL_H

#include "abstracttool.h"
#include <QPointF>
#include <QVector>

// ─────────────────────────────────────────────────────────────────────────
// PolygonTool — 정다각형(POLYGON) 그리기 툴
//
// AutoCAD의 POLYGON 명령과 동일한 흐름으로 동작한다.
//   1) 변의 수 입력 (숫자 입력, Enter=기본값 6)
//   2) 중심점 지정 (클릭 또는 좌표 입력)
//   3) 내접(I)/외접(C) 모드 선택 후 반지름 지정 (클릭 또는 숫자 입력)
//      - 내접(Inscribed) : 입력한 반지름이 "중심→꼭짓점" 거리 (다각형이 원에 내접)
//      - 외접(Circumscribed) : 입력한 반지름이 "중심→변의 중점" 거리 (원이 다각형에 내접)
//      - 반지름을 클릭으로 지정하면 그 클릭 방향이 첫 꼭짓점 방향(회전각)이 된다.
//   완성된 다각형은 닫힌 폴리라인(PolylineItem) 하나로 생성된다.
// ─────────────────────────────────────────────────────────────────────────
class PolygonTool : public AbstractTool {
    Q_OBJECT
public:
    enum State { WaitSides, WaitCenter, WaitRadius };
    enum class Mode { Inscribed, Circumscribed };

    explicit PolygonTool(QObject *parent = nullptr);
    ~PolygonTool();

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    bool processCommand(const QString &command) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

    // 다각형 꼭짓점 좌표 계산 (외부에서도 재사용 가능하도록 static으로 둠)
    //   center   : 중심점
    //   radius   : 입력 반지름 (모드에 따라 중심→꼭짓점 또는 중심→변의 중점 거리)
    //   sides    : 변의 수 (최소 3)
    //   mode     : 내접/외접
    //   rotationRad : 첫 번째 꼭짓점의 각도(라디안, 0 = +X축 방향)
    static QVector<QPointF> computeVertices(const QPointF &center, double radius,
                                            int sides, Mode mode, double rotationRad);

private:
    void showSidesPrompt();
    void showCenterPrompt();
    void showRadiusPrompt();
    void finishPolygon(const QPointF &radiusRefPos, double overrideRadius = -1.0);
    void cancelDrawing();
    void updateGhost(const QPointF &radiusRefPos);

    State   m_state;
    int     m_sides;      // 변의 수 (기본 6)
    Mode    m_mode;       // 내접/외접 모드 (기본 내접)
    QPointF m_center;     // 중심점
    QPointF m_currentPos; // 현재 마우스 위치 (반지름/방향 계산용)
};

#endif // POLYGONTOOL_H
