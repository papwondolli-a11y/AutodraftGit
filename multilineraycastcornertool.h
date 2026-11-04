#ifndef MULTILINERAYCASTCORNERTOOL_H
#define MULTILINERAYCASTCORNERTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"

class LineItem;

// ─────────────────────────────────────────────────────────────────────────────
// MultilineRaycastTCornerTool  —  광선 발사 방식 다중선 T-코너
// ─────────────────────────────────────────────────────────────────────────────
class MultilineRaycastTCornerTool : public AbstractTool {
    Q_OBJECT
public:
    explicit MultilineRaycastTCornerTool(QObject *parent = nullptr);

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

    LineItem *findNearestMultilineItem(const QPointF &pos) const;

    LineItem *m_crossbar      = nullptr;
    QPointF   m_crossbarClick;
};

// ─────────────────────────────────────────────────────────────────────────────
// Command — 툴 등록용
// ─────────────────────────────────────────────────────────────────────────────
class MultilineRaycastTCornerCommand : public CommandBase {
    Q_OBJECT
public:
    explicit MultilineRaycastTCornerCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;
private:
    IAppContext *m_ctx;
};

#endif // MULTILINERAYCASTCORNERTOOL_H
