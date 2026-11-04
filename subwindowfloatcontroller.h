#ifndef SUBWINDOWFLOATCONTROLLER_H
#define SUBWINDOWFLOATCONTROLLER_H

#include <QObject>
#include <QPointer>
#include <QList>

class QMdiArea;
class DrawSubWindow;
class QMdiSubWindow;
class AppContext;
class QEvent;

class SubWindowFloatController : public QObject {
  Q_OBJECT

public:
  explicit SubWindowFloatController(QMdiArea *mdiArea, AppContext *appContext, QObject *parent = nullptr);
  ~SubWindowFloatController();

  // 특정 서브윈도우를 메인 윈도우 바깥으로 분리 (Floating)
  void detachSubWindow(DrawSubWindow *subWindow);

  // 분리된 독립 윈도우를 MDI 메인 영역으로 다시 도킹 (Attach)
  void attachSubWindow(DrawSubWindow *subWindow);

  // 분리/도킹 토글
  void toggleFloat(DrawSubWindow *subWindow);

  // 현재 분리(Floating) 상태인지 여부 확인
  bool isDetached(DrawSubWindow *subWindow) const;

  // 현재 분리되어 바깥에 떠 있는 윈도우 목록
  QList<DrawSubWindow *> detachedWindows() const;

  // [신규] 현재 활성화된 분리 윈도우 관리 (명령어 포커스 우선순위 보장)
  DrawSubWindow *activeFloatingWindow() const;
  void setActiveFloatingWindow(DrawSubWindow *w);
  void clearActiveFloatingWindow();

  // [신규] 서브윈도우 공간 클릭 감시 대상 등록/해제
  void registerSubWindow(DrawSubWindow *subWindow);
  void unregisterSubWindow(DrawSubWindow *subWindow);

  // [신규] 창 공간 클릭 또는 포커스 시 활성화 처리
  void handleSubWindowActivated(DrawSubWindow *subWindow);

  // [신규] 분리창들의 Z-order 계층 정렬 (대시보드창 최하단 < 비활성 분리창 < 활성 분리창 최상단)
  void raiseFloatingWindows(DrawSubWindow *activeSubWindow);

public slots:
  void onDelayedRaiseFloatingTimeout(); // [신규] 탭 클릭 후 OS 포커스 완료 시점 Z-order 보장 슬롯

signals:
  void subWindowDetached(DrawSubWindow *subWindow);
  void subWindowAttached(DrawSubWindow *subWindow);
  void subWindowActivated(DrawSubWindow *subWindow); // [신규] 창 활성화 시그널 (분리창 및 고정창 공용)

protected:
  bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
  void onDetachedWindowDestroyed(QObject *obj);
  void onRegisteredWindowDestroyed(QObject *obj);
  void onDetachRaiseTimeout(); // [신규] 분리 직후 고정창 앞 전면 배치 보장 슬롯

private:
  DrawSubWindow *findDrawSubWindow(QObject *watched) const;
  void installSubWindowFilters(DrawSubWindow *subWindow);
  void removeSubWindowFilters(DrawSubWindow *subWindow);

  QMdiArea *m_mdiArea;
  AppContext *m_appContext;
  QList<QPointer<DrawSubWindow>> m_registeredList;
  QList<QPointer<DrawSubWindow>> m_detachedList;
  QPointer<DrawSubWindow> m_activeFloatingWindow;
  bool m_isActivating = false;
};

#endif // SUBWINDOWFLOATCONTROLLER_H
