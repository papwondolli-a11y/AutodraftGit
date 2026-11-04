#ifndef GHOSTOVERLAY_H
#define GHOSTOVERLAY_H

#include "snaptypes.h"
#include <QPainterPath>
#include <QPixmap>
#include <QVector>
#include <QWidget>

class GhostOverlay : public QWidget {
  Q_OBJECT
public:
  enum SelectionMode { Mode_None, Mode_Window, Mode_Crossing };

  explicit GhostOverlay(QWidget *parent = nullptr);

  void setGhostLine(const QLineF &sceneLine, bool visible,
                    const QColor &color = Qt::gray,
                    const QVector<qreal> &dashPattern = {});

  void setGhostPath(const QPainterPath &scenePath, bool visible,
                    const QColor &color = Qt::gray, double width = 1.0,
                    const QVector<qreal> &dashPattern = {});

  // 블록 미리보기 등 (씬 좌표계 pos, 로컬 bbox blockRect, scale/rotation)
  void setGhostPixmap(const QPixmap &pixmap, const QPointF &pos,
                      const QRectF &blockRect, double scale, double rotation,
                      bool visible);

  void setSnapMarker(const QPointF &scenePos, SnapType type);

  void setSelectionRect(const QPointF &p1, const QPointF &p2,
                        SelectionMode mode);

  // 줌 윈도우용 점선 사각형 (scenePos 기준)
  void setDashRect(const QPointF &p1, const QPointF &p2, const QColor &color);

  // 초기화
  void clear();

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  void drawSnapMarker(QPainter &painter, const QPointF &viewPos, SnapType type);

  // 고스트 라인 (씬 좌표계)
  bool m_ghostLineVisible = false;
  QLineF m_ghostSceneLine;
  QColor m_ghostLineColor = Qt::gray;
  QVector<qreal> m_ghostLineDashPattern;

  // 고스트 패스 (씬 좌표계)
  QPainterPath m_ghostScenePath;
  bool m_ghostPathVisible = false;
  QColor m_ghostPathColor = Qt::gray;
  double m_ghostPathWidth = 1.0;
  QVector<qreal> m_ghostPathDashPattern;

  // 고스트 픽스맵 (씬 좌표계)
  QPixmap m_ghostPixmap;
  QPointF m_ghostPixmapPos;
  QRectF m_ghostPixmapRect;
  double m_ghostPixmapScale = 1.0;
  double m_ghostPixmapRotation = 0.0;
  bool m_ghostPixmapVisible = false;

  // 스냅 (씬 좌표계)
  bool m_snapVisible = false;
  QPointF m_snapScenePos;
  SnapType m_snapType = SnapType::None;

  // 선택 박스 (씬 좌표계)
  QPointF m_selectionP1;
  QPointF m_selectionP2;
  SelectionMode m_selectionMode = Mode_None;

  // 줌 윈도우용 (씬 좌표계)
  bool m_dashRectVisible = false;
  QPointF m_dashP1;
  QPointF m_dashP2;
  QColor m_dashColor;
};

#endif // GHOSTOVERLAY_H
