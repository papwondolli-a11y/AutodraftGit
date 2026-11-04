#ifndef MYGRAPHICSVIEW_H
#define MYGRAPHICSVIEW_H

#include <QGraphicsView>

class GhostOverlay;

class MyGraphicsView : public QGraphicsView {
  Q_OBJECT
public:
  explicit MyGraphicsView(QWidget *parent = nullptr);
  void setupBottomLeftOrigin();
  void updateTheme(class ThemeManager *tm);
  void setCustomCrossCursor();
  void applyArrowCursorOutside();
  void zoomAll();
  void zoomExtend();
  void zoomWindow(const QRectF &rect);
  void zoomToRect(const QRectF &rect);

  // 고스트 라인/패스/픽스맵, 스냅 마커, 선택 박스, 대시 사각형을 그리는 투명
  // 오버레이 위젯. QGraphicsScene/QGraphicsView를 전혀 건드리지 않고 이
  // 위젯만 갱신되므로 도면 크기와 무관하게 항상 가볍다.
  GhostOverlay *ghostOverlay() const { return m_ghostOverlay; }

  // [신규] 분리/도킹 시 OpenGL 컨텍스트 소멸을 방지하고 m_ghostOverlay를 안전하게 보존하며 뷰포트를 재연결
  void recreateOpenGLViewport();

signals:
  // 좌표값 표시(상태바) 용도로만 사용 (로직용 아님)
  void mouseMoved(QPointF pos);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void focusInEvent(QFocusEvent *event) override;
  void setCursor(const QCursor &cursor);
  bool viewportEvent(QEvent *event) override;
  void showEvent(QShowEvent *event) override;
  void leaveEvent(QEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  void enterEvent(QEnterEvent *event) override;
#else
  void enterEvent(QEvent *event) override;
#endif

private:
  bool m_isPanning = false;
  QPointF m_lastPanPosF;
  QPointF m_lastPanScenePos;
  QCursor m_cachedCursor;
  QColor m_lastCacheColor;
  bool m_isInternalCursorChange = false;

  // [핵심] showEvent()의 기본 A3 화면 설정을 "최초 1회"에만 적용하기 위한 플래그.
  // 이 플래그가 없으면 최소화 후 복구 등으로 위젯이 재표시(hide/show)될 때마다
  // 매번 기본 화면으로 리셋되어, zoomAll() 등 다른 줌 상태를 덮어써 버린다.
  bool m_initialShowDone = false;

  GhostOverlay *m_ghostOverlay = nullptr;
};

#endif // MYGRAPHICSVIEW_H
