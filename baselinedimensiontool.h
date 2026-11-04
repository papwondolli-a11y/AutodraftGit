#ifndef BASELINEDIMENSIONTOOL_H
#define BASELINEDIMENSIONTOOL_H

#include "abstracttool.h"
#include "mydimension.h"
#include <QList>

class BaselineDimensionTool : public AbstractTool {
  Q_OBJECT
public:
  explicit BaselineDimensionTool(QObject *parent = nullptr);
  ~BaselineDimensionTool();

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
  void finalizeDimensions(const QPointF &lastPos);
  void updateGhostPreview();

  enum State { WaitFirstOrigin, WaitNextOrigins, WaitDimLinePoint };

  State m_state;
  MyVector m_firstOrigin;
  QList<MyVector> m_nextOrigins;
  QPointF m_currentMousePos;
  bool m_isDrawing;
};

#endif // BASELINEDIMENSIONTOOL_H
