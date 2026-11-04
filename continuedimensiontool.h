#ifndef CONTINUEDIMENSIONTOOL_H
#define CONTINUEDIMENSIONTOOL_H

#include "abstracttool.h"
#include "mydimension.h"
#include <QPointF>

class ContinuedimensionTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ContinuedimensionTool(QObject *parent = nullptr);
    ~ContinuedimensionTool() override;

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
    void selectBaseDimension(const QPointF &pos);
    void finalizeDimensions(const QPointF &lastPos);
    void updateGhostPreview();

    enum State {
        WaitInitialOption,
        WaitBaseDimension,
        WaitFirstOrigin,
        WaitNextOrigins,
        WaitDimLinePoint,
        WaitTotalDimPrompt
    };

    State m_state;
    bool m_isDrawing;
    bool m_useSelectionMode;

    MyVector m_firstOrigin;
    QList<MyVector> m_nextOrigins;
    QList<MyVector> m_allPoints;
    MyVector m_selectedDimLine;
    MyVector m_dimLinePosition;
    QPointF m_currentMousePos;

    double m_fixedAngle;
    bool m_useFixedAngle;
    bool m_isHvMode; // [추가] 수평/수직 모드 여부

    // [핵심 추가] 기준 치수선의 공차, 기호 등 속성을 복사해두기 위한 변수
    MyDimension m_baseDimData;
    QList<class LinearDimensionItem *> m_chainItems; // 생성한 연속 치수 (전체 치수 생성 시 겹치는 보조선 숨김용)
};

#endif // CONTINUEDIMENSIONTOOL_H