#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QPointer>
#include "drawsubwindow.h"

// 전방 선언
class QMdiArea;
class QMdiSubWindow;
class QKeyEvent;
class CommandManager;
class DrawSubWindow;
class CustomStatusBar;
class DocTabWidget;
class AppContext;
class AppCommandRegistry;
class CommandDock;
class CustomMenuBar;
class LineTypeDock;
class QVBoxLayout;
class CustomWindowHandler;
class QToolButton;
class SubWindowControls;
class topToolBar;
class InputManager;
class MdiController;
class PropertyDock; // Added forward declaration
class QStackedWidget;
class DashboardWidget;
class BlockPaletteDock;

class MenuDockSyncController;
class QUndoStack;
class CustomTitleBar;
class WindowGeometryController;
class SubWindowFloatController;

#include "thememanager.h" // ThemeManager::ThemeType 사용을 위해 전체 정의 필요

// AppCommandRegistry는 cpp에서만 include 해도 됩니다.

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();
  //   void addSubWindow(DrawSubWindow* subWindow);
  void addSubWindow(DrawSubWindow *subWindow);

  CommandDock *getCommandDock() const { return m_commandDock; }
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;
#else
  bool nativeEvent(const QByteArray &eventType, void *message, long *result) override;
#endif

  // [필수] AppContext 접근자
  AppContext *getAppContext() const { return m_appContext; }

public slots:
  // + 버튼 눌렀을 때 실행할 슬롯
  void executeTextCommand(const QString &command);
  void showDashboard();
  void showWorkspace();

  // [DXF] Import / Export 직접 구동할 슬롯 추가
  void onActionDxfImport();
  void onActionDxfExport();

protected:
  // [신규] 이벤트 필터 오버라이딩
  bool eventFilter(QObject *watched, QEvent *event) override;
  void changeEvent(QEvent *event) override;
  void closeEvent(QCloseEvent *event) override; // [신규] 확실한 종료를 보장

private:
  // [리팩토링] 로직 위임 매니저
  InputManager *m_inputManager = nullptr;
  MdiController *m_mdiController = nullptr;

  QStackedWidget *m_centralStack = nullptr;
  DashboardWidget *m_dashboard = nullptr;

  QMdiArea *mdiArea;
  CommandManager *cmdManager; // 매니저 보유
  CustomStatusBar *myStatusBar;
  DocTabWidget *docTabWidget;

  AppCommandRegistry *m_cmdRegistry;

  AppContext *m_appContext = nullptr;   // AppContext 소유
  class AiBridgeServer *m_aiBridge = nullptr; // AI(MCP) local bridge server
  CommandDock *m_commandDock = nullptr; // CommandDock 소유
  CustomMenuBar *myMenuBar;
  LineTypeDock *m_lineTypeDock = nullptr;
  PropertyDock *m_propertyDock = nullptr;
  BlockPaletteDock *m_blockPaletteDock = nullptr;

  CustomWindowHandler *m_windowHandler;

  void setupCommandSystem();
 // void toggleMenuBar();
  void setupCustomTitleBar(QVBoxLayout *titleLayout); // 커스텀 타이틀바 설정
  QToolBar *m_titleToolBar;
  QAction *m_undoAction = nullptr;
  QAction *m_redoAction = nullptr;

  CustomTitleBar *m_customTitleBar = nullptr;

  int dwgNo;
  bool m_isInternalChange = false; // [신규] 재진입 방지 가드
  topToolBar *toptoolbar;
  class LayerTableView *m_layerTableView = nullptr;
  QDockWidget *m_layerTypeDock = nullptr;
  MenuDockSyncController *m_menuSyncController = nullptr;

  QToolButton *m_maximizeButton = nullptr;
  SubWindowControls *m_subControls = nullptr;
  SubWindowFloatController *m_floatController = nullptr; // [신규] 서브윈도우 분리/도킹 컨트롤러
  void updateSubWindowControls();
  bool isEveryMdiSubWindowMinimized() const; // [신규] MDI 서브윈도우 전체 최소화 여부 확인

  bool m_waitingForBlockSelection = false; // [신규] insert 명령 후 상태 추적

private slots:
  void onSubWindowMinimize();
  void onSubWindowClose();
  void onSubWindowFloatToggle(); // [신규] 분리/도킹 버튼 클릭 슬롯
  void onTabFloatWindowRequested(QMdiSubWindow *subWindow); // [신규] 탭 컨텍스트 메뉴 분리/도킹 슬롯
  void onSubWindowDetached(DrawSubWindow *subWindow); // [신규] 분리 완료 시 슬롯
  void onSubWindowAttached(DrawSubWindow *subWindow); // [신규] 도킹 완료 시 슬롯
  void onSubWindowFloatingActivated(DrawSubWindow *subWindow); // [신규] 분리 창 활성화 슬롯
  void syncUndoActions(QUndoStack *stack); // [신규] Undo 스택 동기화
  void onCurrentFontChanged(const QString &family, qreal size); // [신규] 폰트 변경 처리

  // [신규] CustomTitleBar의 시그널을 받을 슬롯들 (람다 대체용)
  void onActionSave();
  void onActionSaveAs();
  void onActionPrint();
  void onTitleMinimize();
 // void onTitleMaximize();
  void onTitleClose();

  // [신규] 테마 변경 시 QMdiArea 배경을 코드로 갱신 (CSS는 QMdiArea에 적용 안됨)
  void onMdiThemeChanged(ThemeManager::ThemeType type);
  void restoreFocusToActiveView(); // [신규] 명령 실행 후 도면 뷰 포커스 복원 슬롯
  void onNewWindowRequested();     // [신규] 탭바 새 창 요청 슬롯
  void onSubWindowAttachRestoreTimeout(); // [신규] 도킹 복귀 시 최대화 지연 복원 슬롯
  void openDrawingFile(const QString &path); // [신규] 파일 오픈 및 후처리 전담 슬롯 (람다 대체)

  // [신규] 도면창 간 객체 복사/붙여넣기 (Ctrl+C / Ctrl+V)
  void onCopySelectedToClipboard();
  void onPasteFromClipboard();

  private:
  WindowGeometryController *m_geomController;
  QPointer<DrawSubWindow> m_pendingFocusWindow; // [신규] 포커스 복원 대상 윈도우
  QPointer<DrawSubWindow> m_pendingRestoreSubWindow; // [신규] 도킹 복원 대상 윈도우
};

#endif // MAINWINDOW_H
