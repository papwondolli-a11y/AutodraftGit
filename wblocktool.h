#ifndef WBLOCKTOOL_H
#define WBLOCKTOOL_H

#include "abstracttool.h"
#include <QPointF>

class WBlockTool : public AbstractTool {
  Q_OBJECT
public:
  explicit WBlockTool(QObject *parent = nullptr);
  virtual ~WBlockTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;
  bool processCommand(const QString &command) override;
  void keyPress(QKeyEvent *event) override;

private:
  enum State { State_Idle, State_Selecting, State_WaitingForBase };

  void updatePrompt();
  void handleSelectionPress(QPointF pos, Qt::MouseButton button);
  void handleSelectionMove(QPointF pos);
  void processBoxSelection(QPointF p2);
  void finishSelectionStep();
  void executeWBlock();
  void hideGripsOfSelectedItems();
  void clearOverlay();

  State m_currentState;
  bool m_isBoxSelecting;
  QPointF m_firstCorner;
  QPointF m_basePoint;
  QPointF m_currentMousePos;
};

#endif // WBLOCKTOOL_H
