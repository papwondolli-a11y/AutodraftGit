#ifndef MDICONTROLLER_H
#define MDICONTROLLER_H

#include <QObject>
#include <QPointer>

class QMdiArea;
class QMdiSubWindow;
class SubWindowControls;
class QEvent;

class MdiController : public QObject {
  Q_OBJECT
public:
  explicit MdiController(QMdiArea *mdiArea, SubWindowControls *controls,
                         QObject *parent = nullptr);

  bool handleWindowStateChange(QObject *watched, QEvent *event);
  void syncAllSubWindowStyles();
  void updateSubWindowControls();

  // [신규] 새 서브윈도우 생성 직후 OS 레벨 윈도우 활성화 및 마우스 파이프라인 동기화 전담
  void setupNewSubWindowActivation(QMdiSubWindow *subWindow);
  void scheduleSubWindowActivation(QMdiSubWindow *subWindow, int delayMs = 100);

  void setInternalChange(bool change) { m_isInternalChange = change; }

private slots:
  // 최소화되어 있던 도면 창이 최대화로 복구된 뒤, 뷰가 전체 도면을 보여주도록
  // zoomAll()을 지연 호출하기 위한 슬롯 (람다 대신 멤버 함수 포인터 사용)
  void onZoomAllRestoreTimeout();
  void onUpdateControlsTimeout();
  void onFrameStyleSyncTimeout();
  void onFrameStyleFinalizeTimeout();
  void onNewSubWindowActivateTimeout(); // [신규] 새 창 활성화 및 마우스 동기화 지연 슬롯

private:
  QMdiArea *m_mdiArea;
  SubWindowControls *m_subControls;
  bool m_isInternalChange = false;

  // handleWindowStateChange()에서 최대화 복구를 감지했을 때, zoomAll()을
  // 적용할 대상 서브윈도우를 잠시 기억해두는 포인터
  QPointer<QMdiSubWindow> m_zoomAllPendingSub;
  QPointer<QMdiSubWindow> m_stylePendingSub;
  QPointer<QMdiSubWindow> m_pendingNewSub;
  bool m_stylePendingMaximized = false;
};

#endif // MDICONTROLLER_H
