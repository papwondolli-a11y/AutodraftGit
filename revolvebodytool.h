#ifndef REVOLVEBODYTOOL_H
#define REVOLVEBODYTOOL_H

#include "abstracttool.h"

#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

class HatchItem;

// ─────────────────────────────────────────────────────────────────────────────
// RevolveBodyTool — 메뉴 "3D 기계용 > 단면 회전체 만들기" (명령 rev3d)
//   옵션 [영역(A)/해치(H)], 기본 = 해치
//   해치(H): 해칭 클릭(여러 개, 엔터/우클릭 끝)
//   영역(A): 해치 명령처럼 — 사각 두 점으로 경계 후보 선택 → 내부 공간 클릭(여러 곳, 엔터 끝)
//            또는 [복합모양(B)] = 사각 안 전체 모양. 저장 후 3D 를 만들 때마다 다시 찾음(선을 고치면 따라감)
//   → 회전축: 중심선 클릭 또는 두 점 → 위·아래가 다르면 쪽 선택 → 설정창(이름, 원통면 끼워맞춤)
// ─────────────────────────────────────────────────────────────────────────────
class RevolveBodyTool : public AbstractTool {
    Q_OBJECT
public:
    explicit RevolveBodyTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

private:
    enum State { PickHatch, AreaBox1, AreaBox2, AreaPick, PickAxis1, PickAxis2 };

    HatchItem *hatchAt(const QPointF &pos) const;
    void showPrompt();
    void finishHatches();
    void finishArea(bool complexShape);
    void finishAxis(const QPointF &a0, const QPointF &a1);
    void cancelTool();
    QList<QPainterPath> sectionPaths() const;

    State m_state = PickHatch;
    bool m_area = false;               // 영역 방식
    QList<HatchItem *> m_hatches;
    QPointF m_box1;
    QRectF m_box;
    QList<QPointF> m_picks;            // 영역: 내부 공간 점 (비면 복합모양)
    QPainterPath m_region;             // 영역: 찾은 공간
    QPointF m_axis0;
};

#endif // REVOLVEBODYTOOL_H
