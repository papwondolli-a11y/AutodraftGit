#ifndef EXPLODETOOL_H
#define EXPLODETOOL_H

#include "abstracttool.h"
#include <QPointF>

class ExplodeTool : public AbstractTool {
  Q_OBJECT
public:
  explicit ExplodeTool(QObject *parent = nullptr);
  ~ExplodeTool() override;

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;
  void keyPress(QKeyEvent *event) override;

private:
  void handleSelectionPress(QPointF pos, Qt::MouseButton button);
  void handleSelectionMove(QPointF pos);
  void processBoxSelection(QPointF p2);
  void executeExplode();
  void clearOverlay();

  bool m_isBoxSelecting;
  QPointF m_firstCorner;
};

#endif // EXPLODETOOL_H
