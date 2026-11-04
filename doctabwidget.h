#ifndef DOCTABWIDGET_H
#define DOCTABWIDGET_H

#include <QWidget>
#include <QPointer>
#include <QSet>

class QTabBar;
class QPushButton;
class QMdiSubWindow;
class QFrame;
class AppContext;

class DocTabWidget : public QWidget {
  Q_OBJECT
public:
  explicit DocTabWidget(QWidget *parent = nullptr);

  void setAppContext(AppContext *context) { m_appContext = context; }

  // 외부에서 서브윈도우가 생성되었을 때 호출하는 함수
  void addDoc(QMdiSubWindow *subWindow);

  // 현재 활성화된 서브윈도우에 맞춰 탭을 선택하는 함수
  void setCurrentDoc(QMdiSubWindow *subWindow);

  // [신규] 이름 탭 클릭과 100% 동일한 활성화 루틴을 외부(도킹 완료 등)에서 호출
  void activateDoc(QMdiSubWindow *subWindow);

  // [핵심] 이름 탭을 사용자가 직접 클릭한 것과 100% 동일하게 탭을 클릭 트리거
  void clickDocTab(QMdiSubWindow *subWindow);

  // 서브윈도우 컨트롤(최대화 시 조절 버튼) 설정
  void setSubControls(QWidget *controls);

  // 분리/도킹 상태에 따른 탭 표시 갱신
  void updateTabFloatState(QMdiSubWindow *subWindow, bool isDetached);

  // [신규] 최소화 상태에 따른 탭 표시 갱신 ("_ " 접두어로 최소화된 도면창임을 표시)
  void updateTabMinimizedState(QMdiSubWindow *subWindow, bool isMinimized);

signals:
  // + 버튼 클릭 시 발생하는 시그널
  void newWindowRequested();
  void homeClicked();
  void
  navWorkspaceRequested(); // [신규] 대시보드에서 탭 클릭 시 작업 영역 전환 요청
  void floatWindowRequested(QMdiSubWindow *subWindow); // [신규] 탭 우클릭 메뉴 또는 분리 요청 시그널

private slots:
  // 탭바 UI 동작 처리
  void onTabClicked(int index);
  void onTabCloseRequested(int index);
  void onTabBarCustomContextMenuRequested(const QPoint &pos); // [신규] 탭 우클릭 메뉴

  // 서브윈도우가 닫힐 때(destroyed) 호출될 슬롯
  void onSubWindowDestroyed(QObject *obj);

  // destroyed 시그널 처리 이후(이벤트 루프 복귀 뒤) 남은 탭의 창을 전면에 표시
  void onShowCurrentDocDeferred();

  // [신규] 서브윈도우 제목 변경 시 호출될 슬롯
  void onWindowTitleChanged(const QString &title);

  // [신규] 뷰포트 레이아웃 확정 후 2차 보장을 위한 지연 타이머 슬롯
  void onDelayedActivateDocTimeout();
  void onFinalActivateDocTimeout();

private:
  QTabBar *m_docTabBar;
  QPushButton *m_tabNewButton;
  QPushButton *m_homeButton;
  QWidget *m_subControls = nullptr;
  QFrame *m_separator; // 구분선
  AppContext *m_appContext = nullptr;
  QPointer<QMdiSubWindow> m_pendingActivateSubWindow;
  // [신규] 최소화된 서브윈도우 목록. QTabBar는 탭별 배경색을 직접 지원하지
  // 않아 커스텀 QProxyStyle(DocTabBarStyle)이 이 목록을 참조해 탭 배경을 칠한다.
  QSet<QMdiSubWindow *> m_minimizedSubWindows;
};

#endif // DOCTABWIDGET_H
