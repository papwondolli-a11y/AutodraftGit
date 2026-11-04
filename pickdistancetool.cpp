#include "pickdistancetool.h"
#include "mygraphicsscene.h"
#include <QtMath>

PickDistanceTool::PickDistanceTool(QObject *parent) : AbstractTool(parent) {}

void PickDistanceTool::mousePress(QPointF pos, Qt::MouseButton button) {
  if (button != Qt::LeftButton) {
    emit cancelled();
    return;
  }

  if (!m_firstPointPicked) {
    m_firstPoint = pos;
    m_firstPointPicked = true;
    if (m_scene) {
      m_scene->setGhostLine(QLineF(pos, pos), true);
    }
  } else {
    double dist = QLineF(m_firstPoint, pos).length();
    emit distancePicked(dist);
    if (m_scene) {
      m_scene->clearOverlay();
    }
    m_firstPointPicked = false;
  }
}

void PickDistanceTool::mouseMove(QPointF pos) {
  if (m_firstPointPicked && m_scene) {
    m_scene->setGhostLine(QLineF(m_firstPoint, pos), true);
  }
}
