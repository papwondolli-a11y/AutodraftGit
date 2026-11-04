#ifndef SCALETOOL_H
#define SCALETOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QList>

class CadItem;
class MyGraphicsScene;

class ScaleTool : public AbstractTool {
  Q_OBJECT
public:
  explicit ScaleTool(IAppContext *ctx, QObject *parent = nullptr);
  ~ScaleTool() override;

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  bool processCommand(const QString &command) override;

protected:
  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void keyPress(QKeyEvent *event) override;

private:
  void updatePrompt();
  void clearOverlay();
  void handleSelectionMove(QPointF pos);
  void processBoxSelection(QPointF p2);
  void updateScaleGhost(const QPointF &currentPos);
  void finishScale(double scaleFactor);
  QPointF applyOrtho(const QPointF &pos);

  enum State {
    State_SelectingObjects,
    State_SelectingObjects_SecondCorner,
    State_WaitingForBasePoint,
    State_WaitingForScaleFactor,
    State_WaitingForReferenceLength,
    State_WaitingForNewLength
  };

  State m_currentState = State_SelectingObjects;

  QList<CadItem *> m_selectedItems;
  QPointF m_firstCorner;
  MyVector m_basePoint;
  QPointF m_currentMousePos;

  // 참조 모드 관련 변수
  bool m_isReferenceMode = false;
  double m_referenceLength = 1.0;
  QPointF m_referencePoint1;
};

#endif // SCALETOOL_H
