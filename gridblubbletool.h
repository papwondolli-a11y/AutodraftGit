#ifndef GRIDBLUBBLETOOL_H
#define GRIDBLUBBLETOOL_H

#include "abstracttool.h"
#include "gridbubblesetupdialog.h"
#include "myvector.h"
#include <QPointF>
#include <QString>

// 건축 평면도 주열번호(Grid Bubble) 툴
//
// 워크플로우:
//   "setup" 명령  → GridBubbleSetupDialog (라벨타입·레이어 선택)
//   p1 클릭       → 꼬리 시작점
//   p2 클릭       → 방향 (최초 1회만, 이후 반복 시 생략)
//   반복 p1 클릭  → 직전 방향 재사용, 라벨 자동 증가
class GridBubbleTool : public AbstractTool {
    Q_OBJECT
public:
    using LabelType = GridBubbleSetupDialog::LabelType;

    explicit GridBubbleTool(QObject *parent = nullptr);
    ~GridBubbleTool() override;

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
    void openSetupDialog();
    void cancelDrawing();
    void finalizeBubble();
    void updateGhostPreview(const QPointF &mousePos);
    QString currentLabel() const;
    void    incrementLabel();
    void    showStartPrompt();

    enum State { WaitStart, WaitDirection };

    // ── 상태 ────────────────────────────────────────────────────────────────
    State    m_state;
    MyVector m_pt1;           // 꼬리 시작점
    MyVector m_dirVec;        // 방향 단위벡터 (첫 배치 후 재사용)
    bool     m_hasDirection;  // 방향이 한 번이라도 설정됐는지
    QPointF  m_currentMousePos;

    // ── 설정 (setup 다이얼로그) ───────────────────────────────────────────────
    LabelType m_labelType;
    QString   m_fixedLayer;   // 원 레이어, 비어 있으면 현재 레이어
    QString   m_labelLayer;   // 라벨 레이어 (텍스트)
    bool      m_tiltEnabled;  // 기울기 적용
    double    m_labelSize;    // 라벨 텍스트 크기
    double    m_diameter;     // 원 지름
    int       m_counter;      // 카운터

};

#endif // GRIDBLUBBLETOOL_H
