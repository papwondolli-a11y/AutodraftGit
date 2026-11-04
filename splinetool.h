#ifndef SPLINETOOL_H
#define SPLINETOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <vector>

class SplineItem;

class SplineTool : public AbstractTool {
  Q_OBJECT
public:
  explicit SplineTool(IAppContext *ctx, QObject *parent = nullptr);
  ~SplineTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void keyPress(QKeyEvent *event) override;

  bool processCommand(const QString &command) override;

private:
  void finishSpline(bool closed);
  void cancelDrawing();
  void clearOverlay();

  std::vector<MyVector> m_vertices;
  bool m_isDrawing;
  SplineItem *m_liveSpline;
  QPointF m_currentMousePos;
};

#endif // SPLINETOOL_H
