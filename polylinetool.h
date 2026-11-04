#ifndef POLYLINETOOL_H
#define POLYLINETOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <vector>

class PolylineItem;

class PolylineTool : public AbstractTool {
  Q_OBJECT
public:
  explicit PolylineTool(QObject *parent = nullptr);
  ~PolylineTool();

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  void mousePress(QPointF pos, Qt::MouseButton button) override;
  void mouseMove(QPointF pos) override;
  void keyPress(QKeyEvent *event) override;

  bool processCommand(const QString &command) override;

signals:
  void polylineCreated();

private:
  void finishPolyline(bool closed);
  void cancelDrawing();
  bool undoLastVertex();
  void clearOverlay();

  std::vector<MyVector> m_vertices;
  bool m_isDrawing;
  PolylineItem *m_livePolyline;
  QPointF m_currentMousePos;
};

#endif // POLYLINETOOL_H
