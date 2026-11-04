#ifndef AUTOCONTINUOUSDIMTOOL_H
#define AUTOCONTINUOUSDIMTOOL_H

#include "abstracttool.h"
#include "mydimension.h"
#include "myvector.h"
#include <QList>
#include <QPointF>

// 가상선(1→2)이 씬의 선분들과 만나는 교점을 구해
// 각 교점 구간마다 치수선을 자동 배치하는 툴
class AutoContinuousDimTool : public AbstractTool {
    Q_OBJECT
public:
    explicit AutoContinuousDimTool(QObject *parent = nullptr);
    ~AutoContinuousDimTool() override;

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
    void cancelDrawing();
    void computeIntersections();
    void finalizeDimensions(const QPointF &dimLinePos);
    void updateGhostPreview();
    void buildDimPreview(const QPointF &dimLinePos, QPainterPath &outPath) const;

    enum State {
        WaitStartPoint,
        WaitEndPoint,
        WaitDimLinePoint,
        WaitTotalDimPrompt
    };

    State m_state;
    bool m_isDrawing;

    MyVector m_pt1;
    MyVector m_pt2;
    MyVector m_dimLinePos;

    // 가상선에 수직인 방향: pickExtOrigin 측면 결정 및 전체치수 오프셋 방향으로만 사용
    MyVector m_ghostNormal;
    double m_dimAngle = 0.0; // 치수 측정 방향(deg): 엔티티 수직 기준, 없으면 가상선 각도

    // 교점 리스트 (가상선 방향 파라미터 t 기준 정렬)
    QList<MyVector> m_intersectPts;
    // 각 교점에 대응하는 엔티티의 그립포인트(끝점) 목록
    // 치수보조선 시작점을 교점 대신 엔티티 끝점으로 쓰기 위해 사용
    QList<QList<QPointF>> m_entityGrips;

    QPointF m_currentMousePos;
    MyDimension m_baseDimData;
    QList<class LinearDimensionItem *> m_chainItems; // 생성한 연속 치수 (전체 치수 생성 시 겹치는 보조선 숨김용)
};

#endif // AUTOCONTINUOUSDIMTOOL_H
