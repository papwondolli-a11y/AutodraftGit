#ifndef MULTILINERAYCASTLCORNERTOOL_H
#define MULTILINERAYCASTLCORNERTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "iappcontext.h"

class LineItem;

// ─────────────────────────────────────────────────────────────────────────────
// MultilineRaycastLCornerTool — 광선 발사 방식 다중선 L-코너
// ─────────────────────────────────────────────────────────────────────────────
class MultilineRaycastLCornerTool : public AbstractTool {
    Q_OBJECT
public:
    explicit MultilineRaycastLCornerTool(QObject *parent = nullptr);

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

    LineItem *findNearestMultilineItem(const QPointF &pos) const;

    LineItem *m_firstLine  = nullptr;
    QPointF   m_firstClick;
};

// ─────────────────────────────────────────────────────────────────────────────
// Command — 툴 등록용
// ─────────────────────────────────────────────────────────────────────────────
class MultilineRaycastLCornerCommand : public CommandBase {
    Q_OBJECT
public:
    explicit MultilineRaycastLCornerCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;
private:
    IAppContext *m_ctx;
};

#endif // MULTILINERAYCASTLCORNERTOOL_H
