#ifndef HATCHTOOL_H
#define HATCHTOOL_H

#include "abstracttool.h"
#include <QList>
#include <QRectF>
#include <QPointF>
#include <QString>
#include <QPainterPath>

class CadItem;
class MyGraphicsScene;

class HatchTool : public AbstractTool {
    Q_OBJECT
public:
    enum State {
        SelectingP1,         // 1단계: 첫 번째 코너 선택
        SelectingP2,         // 2단계: 두 번째 코너 선택 (Crossing 영역 / Box 지정)
        PickingHatchPoint    // 3단계: 해칭할 내부점 클릭 (Select 모드 전용)
    };

    enum HatchMode {
        ModeSelect,
        ModeBox
    };

    explicit HatchTool(QObject *parent = nullptr);
    void activate(MyGraphicsScene *scene) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void deactivate() override;

private:
    void executeHatch(const QPointF &pickedPoint);
    void executeBoxHatch(const QRectF &rect);

    // [#8 추가] executeHatch/executeBoxHatch의 공통 로직을 담는 헬퍼 함수
    // HatchItem 생성 → 레이어 펜 적용 → UndoStack push를 일원화합니다.
    void applyHatch(const QPainterPath &boundaryPath);

    State m_currentState;
    QPointF m_p1;
    QPointF m_p2;
    QList<CadItem*> m_selectedItems;

    QString m_patternName;
    double m_patternScale;
    double m_patternAngle;
    HatchMode m_hatchMode;
    QString m_hatchLayer;
    bool m_isDialogShown;
};

#endif // HATCHTOOL_H