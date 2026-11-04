#ifndef MULTILINECORNERTOOL_H
#define MULTILINECORNERTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"

class LineItem;

// ─────────────────────────────────────────────────────────────────────────────
// MultilineCornerTool
//
// 사용법:
//   1) 첫 번째 다중선의 임의 선을 코너 근처에서 클릭
//   2) 두 번째 다중선의 임의 선을 코너 근처에서 클릭
//      → 같은 role 선끼리 교점 계산 후 ExtendEntityCommand 로 트림
//   반복: 우클릭 / Escape 로 종료
// ─────────────────────────────────────────────────────────────────────────────
class MultilineCornerTool : public AbstractTool {
    Q_OBJECT
public:
    explicit MultilineCornerTool(QObject *parent = nullptr);

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
    void doCornerJoin(LineItem *lineA, const QPointF &clickA,
                      LineItem *lineB, const QPointF &clickB);
    void resetState();
    void showPrompt() const;
    QLineF toLineF(LineItem *li) const;

    // 다중선 전용 확장 탐색:
    //   itemAtPos() 가 빈 공간을 클릭했을 때 (2-Line 선 간격 사이 등)
    //   씬 전체에서 ML_DATA_GROUP_ID 가 설정된 LineItem 중 가장 가까운 것을 반환.
    LineItem *findNearestMultilineItem(const QPointF &pos) const;

    // 1차 선택 상태
    LineItem *m_firstLine  = nullptr;
    QPointF   m_firstClick;
};

// ─────────────────────────────────────────────────────────────────────────────
// Command — 툴 등록용 (ToolCommand<> 템플릿 대신 직접 구현)
// ─────────────────────────────────────────────────────────────────────────────
class MultilineCornerCommand : public CommandBase {
    Q_OBJECT
public:
    explicit MultilineCornerCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;
private:
    IAppContext *m_ctx;
};

#endif // MULTILINECORNERTOOL_H
