#ifndef OFFSETTOOL_H
#define OFFSETTOOL_H

#include "abstracttool.h"
#include <QPointer>

class QGraphicsItem;
class CadItem;
class QGraphicsPathItem;

class OffsetTool : public AbstractTool {
  Q_OBJECT

public:
  enum State {
    State_SelectingObject,
    State_WaitingForDistance,
    State_WaitingForSide
  };

  explicit OffsetTool(QObject *parent = nullptr);
  ~OffsetTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  bool processCommand(const QString &command) override;
  void keyPress(QKeyEvent *event) override;

private:
  void clearOverlay();
  void updatePrompt();
  void executeOffset(const QPointF &sidePoint);
  void executeOffsetDirect(const QPointF &targetPoint);

  QPainterPath calculateOffsetPath(CadItem *item, double distance,
                                   const QPointF &sidePoint) const;
  QPainterPath calculateOffsetPathDirect(CadItem *item,
                                         const QPointF &targetPoint) const;

  State m_currentState;
  CadItem *m_selectedItem;
  double m_distance;
  bool m_hasDistance;
  bool m_isThroughMode;

  static double s_lastDistance; // 세션 간 공유되는 마지막 거리값

private:
  void hideGripsOfSelectedItems();
};

#endif // OFFSETTOOL_H
