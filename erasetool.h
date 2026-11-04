#ifndef ERASETOOL_H
#define ERASETOOL_H

#include "abstracttool.h"

class EraseTool : public AbstractTool {
  Q_OBJECT

public:
  explicit EraseTool(QObject *parent = nullptr);
  ~EraseTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;
  void keyPress(QKeyEvent *event) override;
  bool processCommand(const QString &command) override;

private:
  void clearOverlay();
  void handleSelectionPress(QPointF pos, Qt::MouseButton button);
  void handleSelectionMove(QPointF pos);
  void processBoxSelection(QPointF p2);
  void executeErase();

  // 박스 선택용
  bool m_isBoxSelecting;
  QPointF m_firstCorner;
};

#endif // ERASETOOL_H
