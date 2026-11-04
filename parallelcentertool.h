#ifndef PARALLELCENTERTOOL_H
#define PARALLELCENTERTOOL_H

#include "abstracttool.h"

class IAppContext;
class Layer;
class LineItem;

class ParallelCenterTool : public AbstractTool {
    Q_OBJECT

public:
    enum State {
        SelectingFirst,
        SelectingSecond
    };

    // 동심 원/호 선택 항목에서 뽑아낸 중심/반지름/각도 정보.
    // isArc == false 이면 startAngle/spanAngle은 의미 없음(완전한 원).
    struct CircularInfo {
        QPointF center;
        double  radius = 0.0;
        bool    isArc = false;
        double  startAngle = 0.0;
        double  spanAngle = 0.0;
    };

    explicit ParallelCenterTool(QObject *parent = nullptr);
    ~ParallelCenterTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

    // 세션 간 공유 설정값
    static double s_offset;  // 0 = 자동 중심

protected:
    void onFinish() override;
    void onCancel() override;

private:
    void tryCreateCenter();
    void createLineCenter(IAppContext *ctx, LineItem *firstLine, LineItem *secondLine);
    void createCircularCenter(IAppContext *ctx, const CircularInfo &first, const CircularInfo &second);
    void finishNewCenterItem(IAppContext *ctx, Layer *targetLayer, QGraphicsItem *newItem);
    void updatePrompt();
    void resetSelection();
    void openSetupDialog();

    State          m_state;
    QGraphicsItem *m_firstItem;
    QGraphicsItem *m_secondItem;
};

#endif // PARALLELCENTERTOOL_H
