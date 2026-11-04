#ifndef AREACORNERJOINTOOL_H
#define AREACORNERJOINTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include <QRectF>
#include <QPointF>
#include <QLineF>
#include <QList>

class LineItem;

class AreaCornerJoinTool : public AbstractTool
{
    Q_OBJECT
public:
    enum JoinMode {
        LCorner,
        TCorner,
        CCorner
    };

    // 트림 및 분할 연산 정보 구조체
    struct Op {
        LineItem *line;
        QPointF   oldPos;
        QPointF   newPos;
        bool      isFirstEnd;
        LineItem *newLine = nullptr; // 분할 시 생성될 새로운 라인
    };

    explicit AreaCornerJoinTool(JoinMode mode, QObject *parent = nullptr);
    virtual ~AreaCornerJoinTool() = default;

    virtual void activate(MyGraphicsScene *scene) override;
    virtual void deactivate() override;

    virtual void mousePress(QPointF pos, Qt::MouseButton button) override;
    virtual void mouseMove(QPointF pos) override;
    virtual void mouseRelease(QPointF pos, Qt::MouseButton button) override;

private:
    enum State {
        State_WaitingForFirst,
        State_WaitingForSecond
    };

    void resetState();
    void showPrompt() const;
    void doAreaJoin(const QRectF &rect);
    QLineF toLineF(LineItem *li) const;

    bool getIntersection(const QLineF &l1, const QLineF &l2, QPointF &rP) const;
    double getProjT(const QPointF &pt, const QLineF &seg) const;

    JoinMode m_mode;
    State m_currentState;
    QPointF m_firstCorner;
    bool m_isSelecting;
};

// ── AreaCornerJoinCommand ──
class AreaCornerJoinCommand : public CommandBase
{
    Q_OBJECT
public:
    explicit AreaCornerJoinCommand(IAppContext *ctx, AreaCornerJoinTool::JoinMode mode, QObject *parent = nullptr);
    virtual void execute() override;

private:
    IAppContext *m_ctx;
    AreaCornerJoinTool::JoinMode m_mode;
};

#endif // AREACORNERJOINTOOL_H
