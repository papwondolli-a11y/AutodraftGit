#ifndef OVERLAYMANAGER_H
#define OVERLAYMANAGER_H

#include "snaptypes.h"
#include <QColor>
#include <QLineF>
#include <QObject>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QRectF>

class QPainter;

class OverlayManager : public QObject {
  Q_OBJECT
public:
  enum SelectionMode { Mode_None, Mode_Window, Mode_Crossing };

  explicit OverlayManager(QObject *parent = nullptr);

  // 상태 설정 API
  void setGhostLine(const QLineF &sceneLine, bool visible,
                    const QColor &color = Qt::gray,
                    const QVector<qreal> &dashPattern = {});
  void setGhostPath(const QPainterPath &scenePath, bool visible,
                    const QColor &color, double width,
                    const QVector<qreal> &dashPattern = {});
  void setSnapMarker(const QPointF &scenePos, SnapType type);
  void setSelectionRect(const QPointF &p1, const QPointF &p2,
                        SelectionMode mode);
  void setDashRect(const QPointF &p1, const QPointF &p2, const QColor &color);
  void setDrawingLimits(const QRectF &limits);
  void setGhostPixmap(const QPixmap &pixmap, const QPointF &pos,
                      const QRectF &blockRect, double scale, double rotation,
                      bool visible);
  void setSelectionColors(const QColor &windowColor, const QColor &crossingColor);
  void clear();

  // 선택 박스 갱신 시 다시 그려야 하는 영역(이전+현재 사각형의 합집합, 씬 좌표).
  // 씬 전체 update() 대신 이 영역만 invalidate() 하기 위해 사용한다.
  QRectF selectionDirtyRect() const;

  // 실제 드로잉 수행 (MyGraphicsScene::drawForeground에서 호출됨)
  void draw(QPainter *painter, bool showOrigin);

private:
  void drawSnapMarker(QPainter *painter, const QPointF &center, SnapType type);

  // 고스트 라인 상태
  bool m_ghostLineVisible = false;
  QLineF m_ghostSceneLine;
  QColor m_ghostLineColor = Qt::gray;
  QVector<qreal> m_ghostLineDashPattern;

  // 고스트 패스 상태
  bool m_ghostPathVisible = false;
  QPainterPath m_ghostScenePath;
  QColor m_ghostPathColor = Qt::gray;
  double m_ghostPathWidth = 1.0;
  QVector<qreal> m_ghostPathDashPattern;

  // 스냅 마커 상태
  bool m_snapVisible = false;
  QPointF m_snapScenePos;
  SnapType m_snapType = SnapType::None;

  // 선택 박스 상태
  SelectionMode m_selectionMode = Mode_None;
  QPointF m_selectionP1;
  QPointF m_selectionP2;
  QRectF m_prevSelectionRect;  // 직전 프레임의 선택 사각형 (씬 좌표)
  QRectF m_selectionDirty;     // 이전+현재 합집합 = 다시 그릴 영역
  QColor m_selectionWindowColor  = QColor(0, 80, 255);
  QColor m_selectionCrossingColor = QColor(0, 160, 0);

  // 점선 박스 상태
  bool m_dashRectVisible = false;
  QPointF m_dashP1;
  QPointF m_dashP2;
  QColor m_dashColor;

  // [신규] 픽스맵 고스트 상태
  bool m_ghostPixmapVisible = false;
  QPixmap m_ghostPixmap;
  QPointF m_ghostPixmapPos;
  QPointF m_ghostPixmapOffset;
  QRectF m_ghostPixmapRect; // [신규] 블록의 원본 바운딩 박스 (0,0 기준)
  double m_ghostPixmapScale = 1.0;
  double m_ghostPixmapRotation = 0.0;

  // [신규] 드로잉 리미트 (작업 영역)
  QRectF m_drawingLimits;
};

#endif // OVERLAYMANAGER_H
