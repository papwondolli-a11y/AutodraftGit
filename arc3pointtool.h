#ifndef ARC3POINTTOOL_H
#define ARC3POINTTOOL_H

#include "abstracttool.h"
#include <QPointF>

class Arc3PointTool : public AbstractTool {
  Q_OBJECT
public:
  explicit Arc3PointTool(QObject *parent = nullptr);
  ~Arc3PointTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  bool processCommand(const QString &cmd) override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void mouseRelease(QPointF pos, Qt::MouseButton button) override;
  void keyPress(QKeyEvent *event) override;

private:
  void finishArc();
  void cancelDrawing();
  void clearOverlay();

  bool calculateArc(QPointF &center, qreal &radius, qreal &startAngle, qreal &spanAngle) const;

  // 입력 단계: 0(P1 지정 대기), 1(P1 지정 완료 / P2 대기), 2(P2 지정 완료 / P3 대기)
  int m_step;

  QPointF m_p1;
  QPointF m_p2;
  QPointF m_p3;
  QPointF m_currentMousePos;
};

#endif // ARC3POINTTOOL_H
