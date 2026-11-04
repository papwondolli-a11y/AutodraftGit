#include "mygraphicsview.h"
#include "ghostoverlay.h"
#include "thememanager.h"
#include <QCursor>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPaintEvent>
#include <QPainter>
#include <QPixmap>
#include <QScrollBar>
#include <QWheelEvent>
#include <QTimer>

// 도면 뷰포트 종류. 기본은 래스터(일반 위젯): 정확한 커버리지 AA 라 얇은 사선이 고르다.
// OpenGL(QOpenGLWidget)은 화질(AA)이 떨어지지만 대형 도면에서 빠를 수 있어,
// 필요하면 환경변수 AUTODRAFT_OPENGL=1 로 실행해 쓴다.
static bool useOpenGLViewport() {
  return qEnvironmentVariableIntValue("AUTODRAFT_OPENGL") != 0;
}

static QWidget *createViewportWidget() {
  if (useOpenGLViewport())
    return new QOpenGLWidget;
  return new QWidget;
}

MyGraphicsView::MyGraphicsView(QWidget *parent) : QGraphicsView(parent) {
  setViewport(createViewportWidget());
  // [버그 수정] 서브윈도우 최소화 시 이 뷰(및 QOpenGLWidget 뷰포트)가 0 크기로
  // 줄어들면서 FBO 재생성이 실패해 "Framebuffer incomplete" 경고가 발생한다.
  // 최소 크기를 강제해 setGeometry/resize가 절대 0으로 줄이지 못하도록 막는다.
  setMinimumSize(2, 2);
  if (viewport())
    viewport()->setMinimumSize(2, 2);
  setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
  setRenderHint(QPainter::Antialiasing);
  setRenderHint(QPainter::TextAntialiasing);
  setRenderHint(QPainter::SmoothPixmapTransform);
  setAlignment(Qt::AlignCenter);
  // pan/zoom 잔상 문제를 피하기 위해 뷰포트는 항상 전체를 다시 그리게 한다.
  // (고스트/스냅/선택 박스는 이 갱신과 무관한 별도 오버레이 위젯이 처리한다.)
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  setCacheMode(QGraphicsView::CacheNone);

  setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  setupBottomLeftOrigin();

  setMouseTracking(true);
  if (viewport())
    viewport()->setMouseTracking(true);

  setCustomCrossCursor();

  // 고스트 라인/패스/픽스맵, 스냅 마커, 선택 박스, 대시 사각형을 그리는 투명
  // 오버레이 위젯. 뷰포트의 자식으로 얹어서 뷰 위에 항상 떠 있게 한다.
  m_ghostOverlay = new GhostOverlay(viewport());
  m_ghostOverlay->setGeometry(viewport()->rect());

  // [핵심] 씬의 영역을 광활하게 고정하여 뷰의 자동 정렬이나 스크롤 제한에 의한
  // 좌표 밀림 방지. (DrawSubWindow가 씬 sceneRect를 0,0~10000으로 잡기 때문에,
  // 이걸 제거하면 0,0 이하로 pan이 안 되고 zoom window가 sceneRect 밖을 clamp해
  // 엉뚱한 곳을 확대한다. BSP 인덱스는 씬 sceneRect 기준이라 성능과 무관.)
  setSceneRect(-1e7, -1e7, 2e7, 2e7);
}

void MyGraphicsView::recreateOpenGLViewport() {
  // 1. 기존 m_ghostOverlay가 있다면 setViewport 호출 시 자동 소멸(댕글링 포인터)되지 않도록
  //    부모를 임시로 this로 분리하여 안전하게 보호
  if (m_ghostOverlay) {
    m_ghostOverlay->setParent(this);
    m_ghostOverlay->hide();
  }

  // 2. 새 QOpenGLWidget 뷰포트 생성 및 장착 (기존 파괴된 OpenGL 뷰포트는 정리됨)
  QWidget *glWidget = createViewportWidget();
  glWidget->setMouseTracking(true);
  glWidget->setMinimumSize(2, 2); // [버그 수정] 0 크기 리사이즈로 인한 FBO 오류 방지
  setViewport(glWidget);

  // [핵심] setViewport()에 의해 리셋된 CAD 렌더링 힌트, 업데이트 모드, Y축 반전 좌표계 완벽 복원
  setRenderHint(QPainter::Antialiasing);
  setRenderHint(QPainter::TextAntialiasing);
  setRenderHint(QPainter::SmoothPixmapTransform);
  setViewportUpdateMode(QGraphicsView::FullViewportUpdate);
  setupBottomLeftOrigin();

  glWidget->show();
  int targetW = width();
  int targetH = height();
  if (targetW <= 0 || targetH <= 0) {
    if (parentWidget() && parentWidget()->width() > 0 && parentWidget()->height() > 0) {
      targetW = parentWidget()->width();
      targetH = parentWidget()->height();
    }
  }
  if (targetW > 0 && targetH > 0) {
    glWidget->resize(targetW, targetH);
  }

  // 3. m_ghostOverlay의 부모를 새 뷰포트로 안전하게 재연결하고 크기 동기화
  if (m_ghostOverlay) {
    m_ghostOverlay->setParent(viewport());
    m_ghostOverlay->setGeometry(viewport()->rect());
    m_ghostOverlay->show();
  }

  // 4. 커서 및 뷰포트 전체 갱신 (즉시 강제 렌더링)
  setCustomCrossCursor();
  if (viewport()) {
    viewport()->update();
    viewport()->repaint();
  }
  update();
  repaint();
}

void MyGraphicsView::setCustomCrossCursor() {
  int cursorSize = 100;
  int cursorCenter = cursorSize / 2;
  int rectSize = cursorSize * 0.1;

  // 배경에 따라 커서 색상 결정
  QColor cursorColor = Qt::black;
  if (backgroundBrush().color().lightness() < 128) {
    cursorColor = Qt::white;
  }

  // 캐시가 유효하면 재사용
  if (!m_cachedCursor.pixmap().isNull() && m_lastCacheColor == cursorColor) {
    m_isInternalCursorChange = true;
    this->unsetCursor();
    if (viewport())
      viewport()->setCursor(m_cachedCursor);
    m_isInternalCursorChange = false;
    return;
  }

  QPixmap pixmap(cursorSize, cursorSize);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setPen(cursorColor);
  painter.drawLine(cursorCenter, 0, cursorCenter, cursorCenter - rectSize / 2);
  painter.drawLine(cursorCenter, cursorSize, cursorCenter,
                   cursorCenter + rectSize / 2);
  painter.drawLine(0, cursorCenter, cursorCenter - rectSize / 2, cursorCenter);
  painter.drawLine(cursorCenter + rectSize / 2, cursorCenter, cursorSize,
                   cursorCenter);
  painter.drawRect(cursorCenter - rectSize / 2, cursorCenter - rectSize / 2,
                   rectSize, rectSize);

  m_cachedCursor =
      QCursor(pixmap, cursorCenter,
              cursorCenter); // [핵심] 핫스팟을 정중앙으로 명시적 설정
  m_lastCacheColor = cursorColor;

  m_isInternalCursorChange = true;
  this->unsetCursor();
  if (viewport())
    viewport()->setCursor(m_cachedCursor);
  m_isInternalCursorChange = false;
}

void MyGraphicsView::updateTheme(ThemeManager *tm) {
  if (!tm)
    return;
  setBackgroundBrush(tm->canvasBackground());
  setCustomCrossCursor();
}
void MyGraphicsView::resizeEvent(QResizeEvent *event) {
  // [버그 수정] 서브윈도우를 최소화하면 이 뷰(및 QOpenGLWidget 뷰포트)가
  // 폭 또는 높이 0으로 리사이즈되는데, 그 상태로 QOpenGLWidget이 FBO를
  // 재생성하려 하면 "Framebuffer incomplete attachment/missing attachment"
  // 및 "Failed to create wrapper texture" 오류가 발생한다. 크기가 0 이하인
  // 리사이즈는 뷰포트에 전달하지 않고 무시하여 기존 유효한 FBO를 그대로
  // 보존한다 (창이 복원되어 유효한 크기가 되면 정상적으로 다시 반영됨).
  if (event->size().width() <= 0 || event->size().height() <= 0) {
    return;
  }

  QGraphicsView::resizeEvent(event);

  // 오버레이 위젯을 뷰포트 크기에 맞춰 동기화
  if (m_ghostOverlay)
    m_ghostOverlay->setGeometry(viewport()->rect());

  // [핵심] 리사이즈 후 즉시 뷰포트 업데이트 유도
  this->viewport()->update();
}

void MyGraphicsView::focusInEvent(QFocusEvent *event) {
  QGraphicsView::focusInEvent(event);
  if (viewport() && viewport()->underMouse()) {
    setCustomCrossCursor();
  }
}

void MyGraphicsView::setCursor(const QCursor &cursor) {
  if (m_isInternalCursorChange || m_isPanning ||
      cursor.shape() == Qt::ClosedHandCursor ||
      cursor.shape() == Qt::OpenHandCursor) {
    if (viewport())
      viewport()->setCursor(cursor);
    QGraphicsView::setCursor(cursor);
  } else {
    this->unsetCursor();
    if (viewport()) {
      viewport()->setCursor(cursor);
    }
  }
}

bool MyGraphicsView::viewportEvent(QEvent *event) {
  // [보강] 뷰포트 영역에서 마우스가 벗어날 때 즉시 화살표 커서로 복구
  if (event->type() == QEvent::Leave) {
    applyArrowCursorOutside();
    return QGraphicsView::viewportEvent(event);
  }

  // [신규] 뷰포트 영역으로 마우스가 진입할 때 즉시 십자선 커서로 복구
  if (event->type() == QEvent::Enter) {
    setCustomCrossCursor();
    return QGraphicsView::viewportEvent(event);
  }

  return QGraphicsView::viewportEvent(event);
}

void MyGraphicsView::applyArrowCursorOutside() {
  if (m_isPanning)
    return;

  m_isInternalCursorChange = true;
  this->unsetCursor();
  if (viewport())
    viewport()->setCursor(Qt::ArrowCursor);
  m_isInternalCursorChange = false;
}

void MyGraphicsView::leaveEvent(QEvent *event) {
  QGraphicsView::leaveEvent(event);
  applyArrowCursorOutside();
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void MyGraphicsView::enterEvent(QEnterEvent *event) {
#else
void MyGraphicsView::enterEvent(QEvent *event) {
#endif
  QGraphicsView::enterEvent(event);
  setCustomCrossCursor();
}

void MyGraphicsView::setupBottomLeftOrigin() {
  QTransform t;
  t.scale(1.0, -1.0);
  setTransform(t);
}

void MyGraphicsView::mousePressEvent(QMouseEvent *event) {
  // QAbstractScrollArea/QGraphicsView에서 event->pos()는 이미 viewport 로컬 좌표계임
  QPointF scenePos = mapToScene(event->pos());

  // [패닝] 휠 버튼은 View에서 직접 처리
  if (event->button() == Qt::MiddleButton) {
    m_isPanning = true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    m_lastPanPosF = event->position();
#else
    m_lastPanPosF = event->localPos();
#endif
    // [핵심] 팬 시작 시점의 마우스 아래 씬 좌표를 저장 (정밀 앵커)
    setTransformationAnchor(QGraphicsView::NoAnchor);
    m_lastPanScenePos = scenePos;
    setCursor(Qt::ClosedHandCursor);
    event->accept();
    return;
  }

  // [중요] 그 외 버튼은 Scene으로 전달하기 위해 부모 함수 호출
  QGraphicsView::mousePressEvent(event);
}

void MyGraphicsView::mouseMoveEvent(QMouseEvent *event) {
  // 마우스가 뷰포트 영역 밖(메뉴/상단 바 등)으로 이동 중인 경우 즉시 화살표 커서로 복구
  if (viewport() && !viewport()->rect().contains(event->pos())) {
    applyArrowCursorOutside();
  } else {
    // 뷰포트 내부에서 마우스 이동 중 커서가 화살표라면 즉시 십자선으로 복구
    if (!m_isPanning && viewport() && viewport()->cursor().shape() == Qt::ArrowCursor) {
      setCustomCrossCursor();
    }
  }

  QPointF scenePos = mapToScene(event->pos());

  // [패닝 로직 추가]
  if (m_isPanning) {
    // 1. 현재 앵커가 보이는 위치(View Pos) 계산
    QPointF currentViewPos = mapFromScene(m_lastPanScenePos);
    // 2. 마우스와 앵커 사이의 뷰 공간 오차(Delta) 계산
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    QPointF viewDelta = event->position() - currentViewPos;
#else
    QPointF viewDelta = event->localPos() - currentViewPos;
#endif

    // 3. 뷰 공간 오차를 씬 공간 오차로 변환
    QPointF p0 = mapToScene(QPoint(0, 0));
    QPointF p1 = mapToScene(QPoint(viewDelta.x(), viewDelta.y()));
    QPointF sceneDelta = p1 - p0;

    // 4. 행렬 직접 조작
    translate(sceneDelta.x(), sceneDelta.y());

    // 즉시 리페인트 유도
    viewport()->update();
  }

  emit mouseMoved(scenePos);

  // [중요] Scene으로 이벤트 전달
  QGraphicsView::mouseMoveEvent(event);
}

void MyGraphicsView::mouseReleaseEvent(QMouseEvent *event) {
  // [패닝 종료]
  if (event->button() == Qt::MiddleButton) {
    m_isPanning = false;
    // 줌을 위해 다시 AnchorUnderMouse로 복구
    setTransformationAnchor(QGraphicsView::AnchorUnderMouse);
    setCustomCrossCursor();
    event->accept();
    return;
  }

  // [중요] Scene으로 전달
  QGraphicsView::mouseReleaseEvent(event);
}

void MyGraphicsView::wheelEvent(QWheelEvent *event) {
  const double scaleFactor = 1.15;
  if (event->angleDelta().y() > 0) {
    scale(scaleFactor, scaleFactor);
  } else {
    scale(1.0 / scaleFactor, 1.0 / scaleFactor);
  }

  // [핵심] 줌 이후에도 툴(SelectBox 등)이 즉시 반응하도록 강제로 MouseMove
  // 이벤트 유도 position()을 사용하여 줌 후의 정확한 씬 좌표 계산 보장
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  QMouseEvent mouseEvent(QEvent::MouseMove, event->position(),
                         event->globalPosition(), Qt::NoButton, Qt::NoButton,
                         event->modifiers());
#else
  QMouseEvent mouseEvent(QEvent::MouseMove, event->pos(),
                         event->globalPos(), Qt::NoButton, Qt::NoButton,
                         event->modifiers());
#endif
  QGraphicsView::mouseMoveEvent(&mouseEvent);

  event->accept();
}

void MyGraphicsView::zoomAll() {
  if (!scene())
    return;
  QRectF rect = scene()->itemsBoundingRect();
  if (rect.isNull() || !rect.isValid() || rect.isEmpty()) {
    // [수정] 빈 도면일 때 원점(0,0)이 화면 중앙이 아닌 좌측 하단에 위치하도록
    // 기본 A3 작업 영역(-20, -20, 440, 317)으로 맞춤
    rect = QRectF(-20, -20, 440, 317);
  } else {
    double margin = std::max(rect.width(), rect.height()) * 0.1;
    rect.adjust(-margin, -margin, margin, margin);
  }
  zoomToRect(rect);
}

void MyGraphicsView::zoomExtend() { zoomAll(); }

void MyGraphicsView::zoomWindow(const QRectF &rect) {
  if (rect.isValid() && !rect.isNull()) {
    zoomToRect(rect);
  }
}

void MyGraphicsView::zoomToRect(const QRectF &rect) {
  if (rect.isNull() || !rect.isValid())
    return;

  QRectF viewportRect = viewport()->rect();
  if (viewportRect.isEmpty()) {
    // 뷰포트 크기가 레이아웃 중이라 아직 0인 경우, 크기 확정 후 50ms 뒤 자동 재시도
    QTimer::singleShot(50, this, &MyGraphicsView::zoomAll);
    return;
  }

  double scaleX = viewportRect.width() / rect.width();
  double scaleY = viewportRect.height() / rect.height();
  double finalScale = std::min(scaleX, scaleY);

  QTransform t;
  t.scale(finalScale, -finalScale);
  setTransform(t);

  centerOn(rect.center());
  viewport()->update();
}

void MyGraphicsView::showEvent(QShowEvent *event) {
  QGraphicsView::showEvent(event);

  // [핵심] 최소화 후 복구, 탭 전환 등으로 위젯이 다시 show()될 때마다
  // showEvent가 반복 호출되므로, 기본 A3 화면 설정은 "최초 1회"에만 적용한다.
  // 그렇지 않으면 zoomAll() 등으로 맞춰둔 줌 상태가 매번 초기화되어 버린다.
  if (m_initialShowDone)
    return;
  m_initialShowDone = true;

  // 초기 화면 설정: A3 사이즈(420x297)를 기준으로 여백(-20) 포함하여 보여줌
  // 이를 통해 원점이 좌측 하단에 적절히 배치됨
  //
  // [주의] 이 시점에는 MDI 서브윈도우가 아직 최대화되기 전(임시 크기)일 수
  // 있어, 여기서 계산한 맞춤은 최대화 후에는 부정확할 수 있다. 실제 사용자가
  // 보는 "새 도면" 화면의 정확한 맞춤은 최대화가 끝난 뒤
  // DrawSubWindow::scheduleInitialViewFit()(NewSubWindowCommand에서 호출)이
  // 다시 한번 덮어써서 보정한다.
  zoomToRect(QRectF(-20, -20, 440, 317));
}
