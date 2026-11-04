#ifndef PICKDISTANCETOOL_H
#define PICKDISTANCETOOL_H

#include "abstracttool.h"
#include <QObject>

class PickDistanceTool : public AbstractTool {
  Q_OBJECT
public:
  explicit PickDistanceTool(QObject *parent = nullptr);

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;

signals:
  void distancePicked(double distance);
  void cancelled();

private:
  QPointF m_firstPoint;
  bool m_firstPointPicked = false;
};

#endif // PICKDISTANCETOOL_H
