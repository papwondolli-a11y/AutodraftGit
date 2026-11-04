#ifndef MULTILINETCORNERTOOL_H
#define MULTILINETCORNERTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"

class LineItem;

// ─────────────────────────────────────────────────────────────────────────────
// MultilineTCornerTool  —  다중선 T-코너
//
// 사용법:
//   1) 교차선(crossbar, 통과하는 다중선)을 클릭
//   2) 줄기선(stem, 끝이 잘리는 다중선)의 자를 끝 근처를 클릭
//      → 줄기의 각 선이 "클릭 끝에서 가장 가까운 교차선"과의 교점에서 트림
//   반복: 우클릭 / Escape 로 종료
//
// 매칭 원리:
//   groupA(교차선)를 clickB(줄기 클릭)에서의 수직 거리 오름차순 정렬
//   → 가장 가까운 교차선(targetLine)이 줄기 종료 경계
//   줄기의 모든 선이 targetLine과의 교점에서 트림됨
// ─────────────────────────────────────────────────────────────────────────────
class MultilineTCornerTool : public AbstractTool {
    Q_OBJECT
public:
    explicit MultilineTCornerTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;
    bool processCommand(const QString &command) override;

protected:
    void onFinish() override;
    void onCancel() override;

private:
    void doTJoin(LineItem *crossbar, const QPointF &clickA,
                 LineItem *stem,    const QPointF &clickB);
    void resetState();
    void showPrompt() const;
    QLineF toLineF(LineItem *li) const;

    // 씬에서 ML_DATA_GROUP_ID 가 설정된 LineItem 중 pos 에 가장 가까운 것을 반환.
    // itemAtPos() 가 선 위를 정확히 잡지 못할 때의 보조 탐색.
    LineItem *findNearestMultilineItem(const QPointF &pos) const;

    // 1차 선택 — 교차선
    LineItem *m_crossbar      = nullptr;
    QPointF   m_crossbarClick;
};

// ─────────────────────────────────────────────────────────────────────────────
// Command — 툴 등록용
// ─────────────────────────────────────────────────────────────────────────────
class MultilineTCornerCommand : public CommandBase {
    Q_OBJECT
public:
    explicit MultilineTCornerCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;
private:
    IAppContext *m_ctx;
};

#endif // MULTILINETCORNERTOOL_H
