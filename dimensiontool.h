#ifndef DIMENSIONTOOL_H
#define DIMENSIONTOOL_H

#include "abstracttool.h"
#include "mydimension.h"

class DimensionTool : public AbstractTool {
  Q_OBJECT
public:
  explicit DimensionTool(QObject *parent = nullptr);
  ~DimensionTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;

  bool processCommand(const QString &command) override;
  void onFinish() override;
  void onCancel() override;

private:
  void cancelDrawing();
  void finalizeDimension();
  void updateGhostPreview();

  enum State {
    WaitStartPoint,
    WaitEndPoint,
    WaitDimLinePoint,
    WaitCircleDimPoint
  };

  State m_state;
  MyDimension m_data;
  QPointF m_currentMousePos;
  bool m_isDrawing;
};

#endif // DIMENSIONTOOL_H
