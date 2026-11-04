#ifndef GRIDCENTERTOOL_H
#define GRIDCENTERTOOL_H

#include "abstracttool.h"
#include <QPointF>
#include <QVector>

class LineItem;
class Layer;

// 건축 평면 구조 그리드용 중심선 툴.
// 박스(P1,P2)를 지정하면 Setup 다이얼로그 "중심선 간격"에 저장된 X축/Y축 간격값을
// 그대로 사용해 여러 개의 가로/세로 중심선을 즉시 생성한다.
// 간격값이 비어있으면 해당 축은 중앙에 1개만 생성된다.
// Setup에서 치수선 방향을 체크해두면, 중심선 생성 직후 연속치수처럼
// 치수선이 놓일 위치를 클릭(혹은 좌표 입력)으로 지정할 수 있다.
// Setup에서 주열번호 방향을 체크해두면, 치수선 배치가 끝난 뒤 GridBubbleTool과
// 동일한 방식(꼬리선+원+텍스트)으로 그리드 라인마다 주열번호가 자동 생성된다.
class GridCenterTool : public AbstractTool {
    Q_OBJECT

public:
    enum State {
        WaitingP1,
        WaitingP2,
        WaitingHDimPos,   // 상단/하단 치수선 놓일 위치 지정
        WaitingVDimPos    // 좌측/우측 치수선 놓일 위치 지정
    };

    explicit GridCenterTool(QObject *parent = nullptr);
    ~GridCenterTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;

    // 세션 간 공유 설정값 (Setup 다이얼로그 "중심선 간격" 그룹에서 저장)
    static QString s_xSpacingText;  // 가로축(X) 간격 기본값, 콤마 구분
    static QString s_ySpacingText;  // 세로축(Y) 간격 기본값, 콤마 구분

    // 세션 간 공유 설정값 (Setup 다이얼로그 "치수선" 그룹에서 저장)
    static bool s_dimTop;     // 상단 치수선
    static bool s_dimBottom;  // 하단 치수선
    static bool s_dimLeft;    // 좌측 치수선
    static bool s_dimRight;   // 우측 치수선
    static bool s_dimTotal;   // 합계 치수선

    // 세션 간 공유 설정값 (Setup 다이얼로그 "주열번호" 그룹에서 저장)
    static bool s_bubbleTop;      // 상단 주열번호
    static bool s_bubbleBottom;   // 하단 주열번호
    static bool s_bubbleLeft;     // 좌측 주열번호
    static bool s_bubbleRight;    // 우측 주열번호
    static int  s_bubbleTypeH;    // 상단/하단 라벨 타입 (GridBubbleSetupDialog::LabelType)
    static int  s_bubbleTypeV;    // 좌측/우측 라벨 타입

protected:
    void onFinish() override;
    void onCancel() override;

private:
    void computeGridPositions();
    void finalizeGrid();

    void beginDimensionFlow();
    void finishHDimPos(const QPointF &pos);
    void finishVDimPos(const QPointF &pos);
    void finishDimensionFlow();
    void pushDimensionUndo(const QList<QGraphicsItem *> &items);
    Layer *resolveDimLayer(QPen &pen);
    QList<QGraphicsItem *> buildRowDimensions();
    QList<QGraphicsItem *> buildColumnDimensions();
    void updateDimGhost(const QPointF &pos);

    void generateGridBubbles();

    void updateGhostBox(const QPointF &p2);
    void updatePrompt();
    void openSetupDialog();

    State   m_state;
    QPointF m_p1;
    QPointF m_currentMousePos;

    QVector<double> m_xPositions;  // 세로 중심선의 X 좌표 목록
    QVector<double> m_yPositions;  // 가로 중심선의 Y 좌표 목록

    // finalizeGrid()에서 계산되어 치수선/주열번호 배치 단계까지 유지되는 그리드 범위 정보
    double m_vLineMinY = 0.0, m_vLineMaxY = 0.0;  // 세로 중심선의 Y 범위(오버슈트 제외)
    double m_hLineMinX = 0.0, m_hLineMaxX = 0.0;  // 가로 중심선의 X 범위(오버슈트 제외)
    double m_extV = 0.0, m_extH = 0.0;            // 중심선이 P1측 대비 튀어나간 오버슈트 길이
    bool   m_hasWidth = false, m_hasHeight = false;
    bool   m_needVDimPos = false;  // WaitingHDimPos 완료 후 WaitingVDimPos로 이어갈지 여부

    // 중심선의 실제 끝점(오버슈트 포함). 치수선이 생성되지 않은 경우
    // 주열번호는 이 끝점을 기준으로 배치된다.
    double m_topLineY = 0.0, m_bottomLineY = 0.0;
    double m_leftLineX = 0.0, m_rightLineX = 0.0;

    // 치수선이 실제로 생성된 경우, 그 치수선(dimLine)의 좌표와 생성 여부
    double m_topDimY = 0.0, m_bottomDimY = 0.0;
    double m_leftDimX = 0.0, m_rightDimX = 0.0;
    bool   m_topDimSet = false, m_bottomDimSet = false;
    bool   m_leftDimSet = false, m_rightDimSet = false;

    // 합계 치수선이 함께 생성된 경우, 그 합계 치수선(dimLine)의 좌표.
    // 주열번호는 합계 치수선이 있으면 이 좌표를, 없으면 일반 치수선 좌표를 기준으로 삼는다.
    double m_topTotalDimY = 0.0, m_bottomTotalDimY = 0.0;
    double m_leftTotalDimX = 0.0, m_rightTotalDimX = 0.0;
};

#endif // GRIDCENTERTOOL_H
