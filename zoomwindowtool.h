#ifndef ZOOMWINDOWTOOL_H
#define ZOOMWINDOWTOOL_H

#include "abstracttool.h"
#include <QPointF>

class ZoomWindowTool : public AbstractTool {
  Q_OBJECT
public:
  explicit ZoomWindowTool(IAppContext *ctx, QObject *parent = nullptr);

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;
  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

private:
  enum State { State_WaitingForFirst, State_WaitingForSecond };
  State m_currentState = State_WaitingForFirst;
  QPointF m_firstCorner;
  bool m_isSelecting = false;

  void updatePrompt();
  void clearOverlay();
};

#endif // ZOOMWINDOWTOOL_H
