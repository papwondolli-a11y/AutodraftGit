#ifndef MULTILINECCORNERTOOL_H
#define MULTILINECCORNERTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"

class LineItem;

// ─────────────────────────────────────────────────────────────────────────────
// MultilineCCornerTool  —  다중선 C(십자)-코너
//
// 사용법:
//   1) 첫 번째 다중선의 임의 선을 교차 근처에서 클릭
//   2) 두 번째 다중선의 임의 선을 교차 근처에서 클릭
//      → 두 그룹의 모든 선이 same-role 교점에서 분할(split)
//   반복: 우클릭 / Escape 로 종료
//
// 매칭 원리:
//   outer↔outer, mortar↔mortar, space↔space 교점에서 각 선을 분할.
//   같은 role 교점이 없으면 최외곽 선으로 fallback.
// ─────────────────────────────────────────────────────────────────────────────
class MultilineCCornerTool : public AbstractTool {
    Q_OBJECT
public:
    explicit MultilineCCornerTool(QObject *parent = nullptr);

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
    struct SplitOp {
        LineItem *origLine;
        QPointF   origP2;
        QPointF   cutNear;
        LineItem *newLine;
    };

    void doCrossJoin(LineItem *lineA, const QPointF &clickA,
                     LineItem *lineB, const QPointF &clickB);

    // group 의 각 선을 other 에서 same-role 두 선 교점으로 분할.
    // perpOfOther: other 그룹의 수직 방향 (좌/우 탐색용).
    void collectSplitOps(const QList<LineItem *> &group,
                         const QList<LineItem *> &other,
                         const QPointF           &perpOfOther,
                         QList<SplitOp>          &ops);

    void      resetState();
    void      showPrompt() const;
    QLineF    toLineF(LineItem *li) const;
    LineItem *findNearestMultilineItem(const QPointF &pos) const;

    LineItem *m_firstLine  = nullptr;
    QPointF   m_firstClick;
};

// ─────────────────────────────────────────────────────────────────────────────
// Command — 툴 등록용
// ─────────────────────────────────────────────────────────────────────────────
class MultilineCCornerCommand : public CommandBase {
    Q_OBJECT
public:
    explicit MultilineCCornerCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;
private:
    IAppContext *m_ctx;
};

#endif // MULTILINECCORNERTOOL_H
