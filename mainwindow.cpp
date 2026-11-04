#include "mainwindow.h"
#include "abstracttool.h"
#include "appcommandregistry.h" // 등록 클래스 include
#include "appcontext.h"
#include "blockitem.h"
#include "blockpalettedock.h"

#include "caditem.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "commandfactory.h"
#include "commandmanager.h"
#include "aibridgeserver.h"
#include "custommenubar.h"
#include "customstatusbar.h"
#include "customtabwidget.h"
#include "customtitlebar.h"
#include "dashboardwidget.h"
#include "doctabwidget.h"
#include "drawsubwindow.h"
#include "dxfmanager.h" // [신규] DXF 관리자 추가
#include "filecommands.h"
#include "inputmanager.h"
#include "inserttool.h"
#include "layermanager.h"
#include "layertableview.h"
#include "linetypedock.h"
#include "mdicontroller.h"
#include "menudocksynccontroller.h"
#include "mygraphicsscene.h"
#include "mygraphicsview.h"
#include "newsubwindowcommand.h"
#include "propertydock.h"
#include "subwindowcontrols.h"
#include "subwindowfloatcontroller.h"
#include "thememanager.h"
#include "toptoolbar.h"
#include "mdithemesynccontroller.h"
#include "windowgeometrycontroller.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QGraphicsTextItem>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QProgressDialog>
#include <QMessageBox>
#include <QStackedWidget>
#include <QPointer>
#include <QStyle>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>
#include <windows.h>
#include <windowsx.h>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), dwgNo(1) {
  setWindowTitle("CAD Style Interface");
  QApplication::setWindowIcon(QIcon(":/icons/autodraft1.png"));
  setWindowFlags(Qt::FramelessWindowHint | Qt::WindowSystemMenuHint |
                 Qt::WindowMinMaxButtonsHint);
  setMouseTracking(true); // QMainWindow에 마우스 추적 설정

  QWidget *centralContainer = new QWidget(this);
  centralContainer->setCursor(Qt::ArrowCursor);
  QVBoxLayout *layout = new QVBoxLayout(centralContainer);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  m_centralStack = new QStackedWidget(this);
  m_centralStack->setCursor(Qt::ArrowCursor);
  setCentralWidget(m_centralStack);

  // 중앙 MDI 영역
  // [버그 수정] 도면(파일)을 열면 QOpenGLWidget을 뷰포트로 쓰는 MDI 서브윈도우가
  // mdiArea를 꽉 채우는데, 메인 윈도우 레이아웃에 여백이 없어 이 네이티브 자식
  // 창이 MainWindow의 바깥 테두리(리사이즈 판정용 5px 영역, nativeEvent의
  // WM_NCHITTEST 참고)까지 닿아버린다. 마우스가 그 5px 안에 들어와도 Windows가
  // WM_NCHITTEST를 부모(MainWindow)가 아니라 그 자리를 덮고 있는 자식(OpenGL)
  // 창으로 보내버려서, 도면을 연 뒤에는 창 가장자리를 잡아도 리사이즈가 전혀
  // 반응하지 않았다(대시보드에는 이런 네이티브 자식 창이 없어 문제가 없었다).
  // mdiArea를 여백이 있는 래퍼 위젯으로 한 겹 감싸서, 그 자식 창이 테두리
  // 판정 영역을 침범하지 못하게 한다. m_centralStack에는 이 래퍼를 넣고,
  // mdiArea 변수 자체는 그대로 실제 MDI 동작(서브윈도우 추가 등)에 쓴다.
  QWidget *mdiAreaWrapper = new QWidget(m_centralStack);
  mdiAreaWrapper->setCursor(Qt::ArrowCursor);
  QVBoxLayout *mdiAreaWrapperLayout = new QVBoxLayout(mdiAreaWrapper);
  mdiAreaWrapperLayout->setContentsMargins(6, 6, 6, 6);
  mdiArea = new QMdiArea(mdiAreaWrapper);
  mdiArea->setCursor(Qt::ArrowCursor);
  mdiAreaWrapperLayout->addWidget(mdiArea);
  mdiArea->setOption(QMdiArea::DontMaximizeSubWindowOnActivation,
                     true); // 창 전환 시 임의 최대화 방지
  // 상태바
  myStatusBar = new CustomStatusBar(this);
  setStatusBar(myStatusBar);

  // 커맨드 라인
  m_commandDock = new CommandDock(this);
  addDockWidget(Qt::BottomDockWidgetArea, m_commandDock);

  m_appContext = new AppContext(mdiArea, myStatusBar, m_commandDock, this);

  m_dashboard = new DashboardWidget(m_appContext, m_centralStack);
  m_centralStack->addWidget(m_dashboard);
  m_centralStack->addWidget(mdiAreaWrapper);
  m_centralStack->setCurrentWidget(m_dashboard);

  // AppContext의 서브 윈도우 추가 요청을 MainWindow의 addSubWindow에 연결
  connect(m_appContext, &AppContext::requestAddSubWindow, this,
          &MainWindow::addSubWindow);

  // [신규] AppContext의 메뉴/커맨드 활성화 요청 처리 (id 는 QAction::objectName)
  connect(m_appContext, &AppContext::requestSetCommandEnabled, this,
          [this](const QString &id, bool enabled) {
            if (QAction *act = findChild<QAction *>(id))
              act->setEnabled(enabled);
          });

  // [신규] "템플릿 보기 > 교체" 등으로 기본 원형도면이 바뀌면 대시보드 콤보를
  // 즉시 갱신한다 (대시보드가 보이고 있지 않아도 다음에 보일 때 최신 상태).
  connect(m_appContext, &AppContext::requestRefreshDashboardTemplates, this,
          [this]() {
            if (m_dashboard)
              m_dashboard->refreshTemplateCombo();
          });

  // MainWindow::MainWindow 생성자 내부 (ThemeManager 연동 코드 부근)
  if (m_appContext && m_appContext->themeManager()) {
      ThemeManager *tm = m_appContext->themeManager();
      connect(tm, &ThemeManager::themeChanged, this,
              &MainWindow::onMdiThemeChanged);

      // [추가된 코드] 테마 변경 시 창 크기 복원을 담당하는 독립 컨트롤러 생성
      new MdiThemeSyncController(mdiArea, tm, this);

      mdiArea->setBackground(QBrush(tm->windowBackground()));
  }

  //------------------------ custom titleBar ---------------------------
  setupCustomTitleBar(layout);

  // -------  메뉴바  ---------
  myMenuBar = new CustomMenuBar(this);
  layout->addWidget(myMenuBar);

  // 테마 변경 연결
  connect(myMenuBar, &CustomMenuBar::themeSelected, this, [this](int type) {
    if (m_appContext && m_appContext->themeManager()) {
      m_appContext->themeManager()->setTheme((ThemeManager::ThemeType)type);
    }
    // setCheckedTheme는 onTheme* 슬롯 내부에서 이미 호출되므로 중복 불필요
  });

  // ThemeManager 외부에서 테마가 바뀌어도 메뉴 체크 동기화
  if (m_appContext && m_appContext->themeManager()) {
    connect(m_appContext->themeManager(), &ThemeManager::themeChanged,
            this, [this](ThemeManager::ThemeType t) {
              if (myMenuBar)
                myMenuBar->setCheckedTheme(static_cast<int>(t));
            });

    // [추가] 프로그램 시작 시 QSettings 에서 불러온 테마 값으로
    // 메뉴의 체크 상태를 동기화한다 (재시작 후 저장된 테마가
    // 실제로 적용되었는데도 메뉴는 기본값(Draft 기본색상)으로
    // 표시되는 문제 수정).
    myMenuBar->setCheckedTheme(
        static_cast<int>(m_appContext->themeManager()->currentTheme()));
  }

  // 파일 메뉴 연결
  connect(myMenuBar, &CustomMenuBar::newFileRequested, this, [this]() {
    cmdManager->executeStringCommand("new");
    showWorkspace();
  });
  connect(myMenuBar, &CustomMenuBar::openFileRequested, this, [this]() {
    cmdManager->executeStringCommand("open");
    showWorkspace();
  });
  connect(myMenuBar, &CustomMenuBar::saveFileRequested, this,
          [this]() { cmdManager->executeStringCommand("save"); });
  connect(myMenuBar, &CustomMenuBar::saveAsFileRequested, this,
          [this]() { cmdManager->executeStringCommand("saveas"); });

  m_subControls = new SubWindowControls(this);
  m_mdiController = new MdiController(mdiArea, m_subControls, this);
  m_inputManager = new InputManager(m_appContext, this);
  m_floatController = new SubWindowFloatController(mdiArea, m_appContext, this);
  if (m_appContext) {
    m_appContext->setFloatController(m_floatController);
  }

  // -----------------------------------------------------------
  // 0. 창 분리/도킹 (Float / Dock)
  // -----------------------------------------------------------
  connect(m_subControls, &SubWindowControls::floatClicked, this,
          &MainWindow::onSubWindowFloatToggle);

  // -----------------------------------------------------------
  // 1. 최소화 (Minimize) - 통합 슬롯 사용
  // -----------------------------------------------------------
  connect(m_subControls, &SubWindowControls::minimizeClicked, this,
          &MainWindow::onSubWindowMinimize);

  // -----------------------------------------------------------
  // 2. 닫기 (Close) - 통합 슬롯 사용
  // -----------------------------------------------------------
  connect(m_subControls, &SubWindowControls::closeClicked, this,
          &MainWindow::onSubWindowClose);

  // --------- [수정] 탭바 먼저 생성 (컨트롤 버튼을 담기 위해) ---------
  docTabWidget = new DocTabWidget(this);
  docTabWidget->setAppContext(m_appContext);

  // [위등 선정] 탭바 내부로 이동 (파일 바)
  docTabWidget->setSubControls(m_subControls);
  layout->addWidget(myMenuBar);

  // ---------  탭바 레이아웃 추가 ---------
  CustomTabWidget *myTabWidget = new CustomTabWidget(this);
  myTabWidget->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
  layout->addWidget(myTabWidget);

  // 레이어 툴바
  toptoolbar = new topToolBar(m_appContext, this);
  layout->addWidget(toptoolbar);

  layout->addWidget(docTabWidget);

  // 6. 다 쌓인 레이아웃을 QMainWindow 상단 툴바 영역에 고정
  QToolBar *tabBarToolBar = new QToolBar("TabBar", this);
  tabBarToolBar->addWidget(centralContainer);
  tabBarToolBar->setMovable(false); // 툴바를 고정합니다.
  addToolBar(Qt::TopToolBarArea, tabBarToolBar);

  cmdManager = new CommandManager(m_appContext, myMenuBar, myTabWidget, this);
  // 커멘드 라인
  setupCommandSystem();

  // AI(MCP) bridge: local-only server
  m_aiBridge = new AiBridgeServer(m_appContext, this);
  connect(m_aiBridge, &AiBridgeServer::newDrawingRequested, this,
          &MainWindow::onNewWindowRequested);
  connect(m_aiBridge, &AiBridgeServer::drawingOpened, this,
          &MainWindow::showWorkspace);
  if (m_aiBridge->start())
    m_commandDock->logMessage(QStringLiteral("AI bridge server started: ") + m_aiBridge->serverName(), CommandDock::Info);

  // 기존의 toggleMenuBar() 호출 대신 컨트롤러 객체 생성
  m_menuSyncController = new MenuDockSyncController(
      myMenuBar, m_commandDock, m_lineTypeDock, m_layerTypeDock,
      m_blockPaletteDock, m_propertyDock, m_appContext, this);
  // toggleMenuBar();

  // [최종] 전역 키보드 입력을 가로채기 위해 필터 설치
  qApp->installEventFilter(this);

  // [신규] 도면창(서브윈도우) 간 객체 복사/붙여넣기 (Ctrl+C / Ctrl+V).
  // CustomTitleBar의 Undo/Redo 액션과 동일한 패턴: Qt::ApplicationShortcut로
  // 등록해 어느 도면창에 포커스가 있든 전역으로 동작하게 한다.
  QAction *copyAction = new QAction(this);
  copyAction->setShortcut(QKeySequence::Copy); // Ctrl+C
  copyAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(copyAction);
  connect(copyAction, &QAction::triggered, this,
          &MainWindow::onCopySelectedToClipboard);

  QAction *pasteAction = new QAction(this);
  pasteAction->setShortcut(QKeySequence::Paste); // Ctrl+V
  pasteAction->setShortcutContext(Qt::ApplicationShortcut);
  addAction(pasteAction);
  connect(pasteAction, &QAction::triggered, this,
          &MainWindow::onPasteFromClipboard);

  if (m_appContext && m_appContext->snapManager()) {
    myStatusBar->setupSnapControls(m_appContext->snapManager());
  }

  // [핵심] QMdiArea::setBackground()는 CSS로 제어 불가 → 코드로 연결
  if (m_appContext && m_appContext->themeManager()) {
    ThemeManager *tm = m_appContext->themeManager();
    connect(tm, &ThemeManager::themeChanged, this,
            &MainWindow::onMdiThemeChanged);
    // 초기값 즉시 적용
    mdiArea->setBackground(QBrush(tm->windowBackground()));
  }
  // (2) 명령어 등록소(Registry)를 통해 명령어 일괄 등록
  CommandFactory factory;
  AppCommandRegistry registry;
  registry.registerAllCommands(cmdManager, m_appContext, factory);

  connect(mdiArea, &QMdiArea::subWindowActivated, docTabWidget,
          &DocTabWidget::setCurrentDoc);

  // [신규] 탭바 네비게이션 연결
  connect(docTabWidget, &DocTabWidget::homeClicked, this,
          &MainWindow::showDashboard);
  connect(docTabWidget, &DocTabWidget::navWorkspaceRequested, this,
          &MainWindow::showWorkspace);
  connect(docTabWidget, &DocTabWidget::floatWindowRequested, this,
          &MainWindow::onTabFloatWindowRequested);

  // [신규] 플로팅 컨트롤러 상태 변화를 탭바에 반영
  connect(m_floatController, &SubWindowFloatController::subWindowDetached, this,
          &MainWindow::onSubWindowDetached);
  connect(m_floatController, &SubWindowFloatController::subWindowAttached, this,
          &MainWindow::onSubWindowAttached);
  connect(m_floatController, &SubWindowFloatController::subWindowActivated, this,
          &MainWindow::onSubWindowFloatingActivated);

  // 최근 파일 연결
  if (m_appContext) {
    connect(m_appContext, &AppContext::recentFileAdded, myMenuBar,
            &CustomMenuBar::addRecentFile);
    connect(m_appContext, &AppContext::currentFontChanged, this,
            &MainWindow::onCurrentFontChanged);
  }
  // 파일 열기 + 작업 영역 전환(대시보드→MDI, 큰 도면의 첫 렌더링이 여기서
  // 발생)까지 하나의 진행 다이얼로그로 감싼다. 다이얼로그가 execute() 안에서만
  // 떠 있으면 showWorkspace()로 전환되는 순간(첫 화면 렌더링)은 안내 없이
  connect(myMenuBar, &CustomMenuBar::recentFileSelected, this,
          &MainWindow::openDrawingFile);

  // 대시보드 파일 선택 시 연결
  connect(m_dashboard, &DashboardWidget::fileSelected, this,
          &MainWindow::openDrawingFile);

  // [신규] 탭바의 + 버튼 누르면 -> 새창 생성
  connect(docTabWidget, &DocTabWidget::newWindowRequested, this,
          &MainWindow::onNewWindowRequested);

  // [버그 수정] 서브윈도우가 파괴되는 도중(~QObject의 ChildRemoved 처리 시점)에
  // QMdiArea가 subWindowActivated를 emit하면, 직접 연결된 슬롯이 이미 자식
  // (undoStack/layerManager/scene 등)이 삭제된 창의 상태를 읽어 크래시한다.
  // 이벤트 루프로 복귀한 뒤 실행되도록 QueuedConnection으로 연결한다.
  connect(mdiArea, &QMdiArea::subWindowActivated, this,
          &MainWindow::updateSubWindowControls, Qt::QueuedConnection);

  // [추가] 초기 실행 시 서브윈도우가 없으므로 컨트롤 버튼을 숨김 상태로 동기화
  updateSubWindowControls();
}

MainWindow::~MainWindow() {}

// [핵심 2] 키 입력 및 MDI 상태 가로채기 위임
bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
  if (m_isInternalChange)
    return false;

  // 1. MDI 상태 변화는 MdiController가 처리
  if (event->type() == QEvent::WindowStateChange) {
    return m_mdiController->handleWindowStateChange(watched, event);
  }

  // 2. 키 입력은 InputManager가 처리
  if (event->type() == QEvent::KeyPress) {
    return m_inputManager->handleKeyPress(watched,
                                          static_cast<QKeyEvent *>(event));
  }

  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::setupCommandSystem() {
  // 라인타입도크
  m_lineTypeDock = new LineTypeDock(m_appContext, this);
  m_lineTypeDock->setAllowedAreas(Qt::LeftDockWidgetArea |
                                  Qt::RightDockWidgetArea);

  addDockWidget(Qt::RightDockWidgetArea, m_lineTypeDock);
  // 레이어 도크
  m_layerTableView = new LayerTableView(m_appContext, this);
  m_layerTypeDock = new QDockWidget(tr("레이어 목록"), this);
  m_layerTypeDock->setObjectName("LayerTypeDock");
  m_layerTypeDock->setWidget(m_layerTableView);
  addDockWidget(Qt::RightDockWidgetArea, m_layerTypeDock);

  tabifyDockWidget(m_lineTypeDock, m_layerTypeDock);

  // 속성 도크를 레이어 도크 아래에 배치
  m_propertyDock = new PropertyDock(m_appContext, this);
  addDockWidget(Qt::RightDockWidgetArea, m_propertyDock);

  // 블록 팔레트 도크 생성 및 탭 통합
  m_blockPaletteDock = new BlockPaletteDock(m_appContext, this);

  addDockWidget(Qt::RightDockWidgetArea, m_blockPaletteDock);

  tabifyDockWidget(m_lineTypeDock, m_layerTypeDock);
  tabifyDockWidget(m_layerTypeDock, m_blockPaletteDock);
  tabifyDockWidget(m_blockPaletteDock, m_propertyDock);

  // 컨텍스트에서 팔레트 표시 요청 처리
  if (m_appContext) {
    connect(m_appContext, &AppContext::requestShowBlockPalette, this, [this]() {
      if (m_blockPaletteDock) {
        m_blockPaletteDock->setVisible(true);
        m_blockPaletteDock->raise();
        m_blockPaletteDock->setFocus();
        m_waitingForBlockSelection = true; // [추가] insert 명령 시 플래그 설정
      }
    });

    connect(m_appContext, &AppContext::requestActivateBlockLibrary, this,
            [this](const QString &path) {
              if (m_blockPaletteDock) {
                m_blockPaletteDock->activateFolder(path);
                m_blockPaletteDock->setVisible(true);
                m_blockPaletteDock->raise();
              }
            });


    // 다른 툴이나 명령 실행 시 플래그 초기화 (AppContext의 시그널 등을 활용
    // 가능) 여기서는 간단히 툴 변경 시점을 잡거나 할 수 있음.
  }

  auto handleBlockSelection = [this](const QString &path) {
    if (path.isEmpty())
      return;
    if (m_appContext && m_appContext->activeDrawWindow()) {
      InsertTool *tool = new InsertTool(this);
      tool->setContext(m_appContext);
      tool->setTargetLayer(m_blockPaletteDock->selectedLayerName());

      if (path.startsWith("internal:")) {
        QString blockName = path.mid(9);
        // 현재 씬에서 해당 이름의 블록을 찾아 아이템들을 복제합니다.
        MyGraphicsScene *scene = m_appContext->activeScene();
        if (scene) {
          BlockItem *found = nullptr;
          for (QGraphicsItem *item : scene->items()) {
            if (BlockItem *bi = dynamic_cast<BlockItem *>(item)) {
              if (bi->blockName() == blockName) {
                found = bi;
                break;
              }
            }
          }

          if (found) {
            QList<CadItem *> clonedItems;
            for (CadItem *child : found->getItems()) {
              clonedItems.append(child->clone());
            }
            tool->setBlockItems(clonedItems, blockName, found->layerName());
          }
        }
      } else {
        tool->setBlockPath(path);
      }

      m_appContext->activeDrawWindow()->setTool(tool);
    }
    m_waitingForBlockSelection = false; // 선택 완료 후 플래그 해제
  };

  // 블록 선택 시 Insert 명령 실행 연결
  connect(m_blockPaletteDock, &BlockPaletteDock::blockClicked, this,
          [this, handleBlockSelection](const QString &path) {
            if (m_waitingForBlockSelection) {
              handleBlockSelection(path);
            }
          });

  connect(m_blockPaletteDock, &BlockPaletteDock::blockDoubleClicked, this,
          [this, handleBlockSelection](const QString &path) {
            Q_UNUSED(this)
            handleBlockSelection(path);
          });

  toptoolbar->connectComboBox(m_layerTableView);
  connect(toptoolbar, &topToolBar::tableCurrentCheck, this,
          [this](const QString &nameToCheck) {
            if (m_appContext && m_appContext->layerManager()) {
              m_appContext->layerManager()->setCurrentLayer(nameToCheck);
            }
          });

  // 1. command Line Dock
  // (m_commandDock 생성은 AppContext 생성 전으로 이동됨)

  // 2. 시그널 연결 (Dock -> MainWindow)
  connect(m_commandDock, &CommandDock::commandSignal, this,
          &MainWindow::executeTextCommand);
}

void MainWindow::addSubWindow(DrawSubWindow *subWindow) {
  if (!subWindow || !mdiArea)
    return;

  // [핵심] 첫 번째 서브윈도우 생성 시에도 MDI 영역을 먼저 화면에 표시하여
  // 뷰포트 크기와 레이아웃이 확정된 상태에서 서브윈도우가 추가/최대화/포커스되도록 보장
  m_centralStack->setCurrentWidget(mdiArea->parentWidget());
  if (m_appContext)
    m_appContext->setCommandEnabled(QStringLiteral("menu.file.newfromtemplate"), false);

  subWindow->setContext(m_appContext);

  m_isInternalChange = true;
  QMdiSubWindow *wrapper = mdiArea->addSubWindow(subWindow);
  wrapper->setObjectName("DrawSubWindow");
  wrapper->setAttribute(Qt::WA_StyledBackground, true);
  if (!wrapper) {
    m_isInternalChange = false;
    return;
  }

  wrapper->installEventFilter(this);
  wrapper->setProperty("isInitializing", false);

  QSize mdiSize = mdiArea->viewport()->size();
  if (mdiSize.isValid()) {
    wrapper->resize(mdiSize * 0.8);
  } else {
    wrapper->resize(800, 600); // 대비용 기본값
  }

  // 초기 생성 시 최대화 (DrawSubWindow는 이미 FramelessWindowHint로 생성됨)
  wrapper->showMaximized();

  if (mdiArea) {
    mdiArea->setActiveSubWindow(wrapper);
  }
  wrapper->activateWindow();
  this->activateWindow();

  if (subWindow->view()) {
    subWindow->view()->setFocus();
  }

  QString title = tr("Drawing %1").arg(dwgNo++);
  subWindow->setWindowTitle(title);

  connect(subWindow, &DrawSubWindow::coordinateChanged, myStatusBar,
          &CustomStatusBar::updateCoordinate);
  if (subWindow->scene()) {
    connect(subWindow->scene(), &MyGraphicsScene::drawingSettingsChanged, this,
            [this, subWindow]() {
              if (mdiArea && mdiArea->activeSubWindow() == subWindow &&
                  myStatusBar) {
                myStatusBar->updateScale(subWindow->scene()->drawingScale());
                myStatusBar->updateSize(subWindow->scene()->drawingSizeName());
                myStatusBar->updateLinearUnit(subWindow->scene()->linearUnit());
              }
            });
  }
  docTabWidget->addDoc(subWindow);

  if (m_floatController) {
    m_floatController->registerSubWindow(subWindow);
  }

  m_isInternalChange = false;

  // [핵심] MDI 영역에 서브윈도우가 등록된 후(isEmpty() == false) 작업 영역으로 전환
  showWorkspace();
  updateSubWindowControls();

  // 새로 생성된 서브윈도우의 탭 활성화 및 뷰포트 렌더링 트리거
  if (docTabWidget) {
    docTabWidget->clickDocTab(subWindow);
  }

  // [전담 위임] MdiController에서 새 서브윈도우의 OS 레벨 윈도우 활성화 및 마우스 파이프라인 동기화 전담
  if (m_mdiController) {
    m_mdiController->setupNewSubWindowActivation(wrapper);
  }
}

void MainWindow::executeTextCommand(const QString &fullCommand) {
  // 1. 명령 실행
  if (cmdManager) {
    cmdManager->executeStringCommand(fullCommand);
  }

  // 2. [수정] 명령 실행 후 도면(View)에 포커스 주기
  if (m_appContext) {
    DrawSubWindow *dw = m_appContext->activeDrawWindow();

    if (dw && dw->view()) {
      m_pendingFocusWindow = dw;

      bool isFloating = (m_floatController && m_floatController->isDetached(dw)) || (dw->parentWidget() == nullptr);
      if (!isFloating && mdiArea) {
        mdiArea->setActiveSubWindow(dw);
      } else {
        dw->activateWindow();
        dw->raise();
      }

      // [핵심] 커서는 즉시 변경하여 깜빡임 방지
      dw->view()->setCustomCrossCursor();

      // [핵심] 포커스는 비동기로 처리하여 "Double Enter/Space" 현상 방지
      // 사용자 규칙 준수: 람다 대신 멤버 함수 포인터 사용
      QTimer::singleShot(0, this, &MainWindow::restoreFocusToActiveView);

      if (m_propertyDock) {
        m_propertyDock->setSelectionManager(dw->selectionManager());
      }
    } else {
      if (toptoolbar) {
        toptoolbar->updateLayerModel();
      }
      if (m_layerTableView) {
        m_layerTableView->setLayerManager(nullptr);
      }
      if (m_blockPaletteDock) {
        m_blockPaletteDock->setLayerManager(nullptr);
      }
      if (m_propertyDock) {
        m_propertyDock->setSelectionManager(nullptr);
      }
    }
  }
}

// ==========================================================
// [개선된 setupCustomTitleBar]
// 독립된 CustomTitleBar를 생성하고 시그널만 연결합니다.
// ==========================================================
void MainWindow::setupCustomTitleBar(QVBoxLayout *layout) {
  m_customTitleBar = new CustomTitleBar(this);

    m_geomController = new WindowGeometryController(this, this);
    // [수정] 고정 1024x768 대신 현재 화면 작업 영역의 2/3 크기로 복원되도록 한다
    // (노트북처럼 화면이 작은 환경에서 고정 크기가 화면보다 크거나 비율이 안
    // 맞아 보이던 문제 수정).
    m_geomController->setCustomNormalSizeByScreenRatio(2.0 / 3.0);
    // [버그 수정] CustomTitleBar는 항상 "□"로 생성되는데, 앱은 항상 최대화 상태로
    // 시작한다(main.cpp의 showMaximized() 예약). 첫 토글 전까지는
    // WindowGeometryController::maximizedStateChanged가 한 번도 발생하지 않아
    // 아이콘이 실제 상태(최대화)와 반대로 "□"인 채 남아있던 문제를 바로잡는다.
    m_customTitleBar->updateMaximizeButtonIcon(true);

  // 글로벌 단축키 적용을 위해 메인 윈도우에 Undo/Redo 액션 추가
  this->addAction(m_customTitleBar->undoAction());
  this->addAction(m_customTitleBar->redoAction());

  if (m_appContext && m_appContext->undoStack()) {
    syncUndoActions(m_appContext->undoStack());
  }

  // 람다 함수 대신 멤버 함수 포인터를 사용하여 시그널-슬롯 연결
  connect(m_customTitleBar, &CustomTitleBar::saveRequested, this,
          &MainWindow::onActionSave);
  connect(m_customTitleBar, &CustomTitleBar::saveAsRequested, this,
          &MainWindow::onActionSaveAs);
  connect(m_customTitleBar, &CustomTitleBar::printRequested, this,
          &MainWindow::onActionPrint);
  connect(m_customTitleBar, &CustomTitleBar::minimizeRequested, this,
          &MainWindow::onTitleMinimize);

  // connect(m_customTitleBar, &CustomTitleBar::maximizeRequested, this,
  //         &MainWindow::onTitleMaximize);
  connect(m_customTitleBar, &CustomTitleBar::maximizeRequested,
          m_geomController, &WindowGeometryController::toggleMaximizeRestore);
  // [버그 수정] 아이콘은 changeEvent(WindowStateChange)의 isMaximized() 재조회가
  // 아니라, WindowGeometryController가 방금 확정한 상태로 직접 갱신한다(도면을
  // 열어 QOpenGLWidget 뷰포트의 MDI 서브윈도우가 함께 최대화돼 있을 때, 첫 토글
  // 사이클에서만 changeEvent 쪽이 한 박자 늦은 값을 읽어와 아이콘이 실제 화면
  // 전환과 어긋나는 문제가 있었다).
  connect(m_geomController, &WindowGeometryController::maximizedStateChanged,
          m_customTitleBar, &CustomTitleBar::updateMaximizeButtonIcon);

  connect(m_customTitleBar, &CustomTitleBar::closeRequested, this,
          &MainWindow::onTitleClose);

  layout->addWidget(m_customTitleBar);
}

void MainWindow::onActionSave() {
  if (cmdManager) {
    cmdManager->executeStringCommand("save");
  }
}

void MainWindow::onActionSaveAs() {
  if (cmdManager) {
    cmdManager->executeStringCommand("saveas");
  }
}

void MainWindow::onActionPrint() {
  // [주의] "윈도우 지정 출력"을 고르면 사용자가 화면에서 사각형을 다 그릴 때까지
  // execute()가 비동기로 대기해야 하므로, 스택이 아니라 힙에 할당하고 스스로
  // deleteLater()로 정리하게 한다 (FilePrintCommand::execute() 참고).
  FilePrintCommand *printCmd = new FilePrintCommand(m_appContext, this);
  printCmd->execute();
}

void MainWindow::onTitleMinimize() { showMinimized(); }

// void MainWindow::onTitleMaximize() {
//   if (isMaximized()) {
//     showNormal();
//   } else {
//     showMaximized();
//   }
// }

void MainWindow::onActionDxfImport() {
  if (!m_appContext) return;
  DrawSubWindow* dw = m_appContext->activeDrawWindow();
  if (!dw || !dw->scene()) {
      QMessageBox::warning(this, tr("오류"), tr("도면 창이 활성화되어 있지 않습니다."));
      return;
  }
  QString filePath = QFileDialog::getOpenFileName(this, tr("DXF 가져오기"), "", tr("DXF 파일 (*.dxf)"));
  if (filePath.isEmpty()) return;

  DxfManager manager(this);
  if (manager.importFromDxf(filePath, dw->scene())) {
      QMessageBox::information(this, tr("성공"), tr("DXF 파일을 성공적으로 불러왔습니다."));
  } else {
      QMessageBox::warning(this, tr("실패"), tr("DXF 파일 불러오기에 실패했습니다."));
  }
}

void MainWindow::onActionDxfExport() {
  if (!m_appContext) return;
  DrawSubWindow* dw = m_appContext->activeDrawWindow();
  if (!dw || !dw->scene()) {
      QMessageBox::warning(this, tr("오류"), tr("도면 창이 활성화되어 있지 않습니다."));
      return;
  }
  QString filePath = QFileDialog::getSaveFileName(this, tr("DXF 내보내기"), "", tr("DXF 파일 (*.dxf)"));
  if (filePath.isEmpty()) return;

  DxfManager manager(this);
  if (manager.exportToDxf(filePath, dw->scene())) {
      QMessageBox::information(this, tr("성공"), tr("DXF 파일을 성공적으로 저장했습니다."));
  } else {
      QMessageBox::warning(this, tr("실패"), tr("DXF 파일 저장에 실패했습니다."));
  }
}


void MainWindow::onTitleClose() { close(); }

// ==========================================================
// [수정된 윈도우 상태 이벤트]
// [버그 수정] 타이틀바 아이콘은 여기서 isMaximized()를 다시 읽어 갱신하지 않는다.
// setupCustomTitleBar()에서 WindowGeometryController::maximizedStateChanged
// 시그널로 직접 갱신하도록 통일했다 — 이 자리에서 함께 갱신하면, 도면을 열어
// QOpenGLWidget 뷰포트의 MDI 서브윈도우가 함께 최대화돼 있을 때 이 changeEvent
// 쪽이 한 박자 늦은(우리가 방금 지시한 것과 다른) isMaximized() 값을 읽어와
// 방금 정확히 맞춰둔 아이콘을 도로 덮어써버리는 경쟁이 있었다.
// ==========================================================
void MainWindow::changeEvent(QEvent *event) {
  QMainWindow::changeEvent(event);
}

void MainWindow::closeEvent(QCloseEvent *event) {
    if (mdiArea) {
        // 1. 모든 서브윈도우에게 닫기 명령을 내립니다.
        mdiArea->closeAllSubWindows();

        // 2. 닫기를 거부하고 화면에 여전히 살아남은(Visible) 창이 있는지 확인합니다.
        QList<QMdiSubWindow *> subWindows = mdiArea->subWindowList();
        bool hasUnclosedWindow = false;

        // C++11 detach 경고 방지를 위한 인덱스 기반 루프
        for (int i = 0; i < subWindows.size(); ++i) {
            if (subWindows[i]->isVisible()) {
                hasUnclosedWindow = true;
                break;
            }
        }

        // 3. 만약 단 하나라도 저장을 취소해서 창이 눈에 보인다면 메인 윈도우 종료를 취소합니다.
        if (hasUnclosedWindow) {
            event->ignore();
            return;
        }
    }

    // 4. 거부한 창이 없다면(모두 저장했거나 무시했다면) 정상 종료합니다.
    event->accept();

    // 이벤트 루프를 강제로 탈출하여 확실하게 프로그램을 종료시킵니다.
    qApp->quit();
}
// void MainWindow::closeEvent(QCloseEvent *event) {
//   qDebug() << "MainWindow::closeEvent called";
//   // 모든 자식 및 이벤트 루프를 확실히 종료
//   // qApp->quit() 만으로 부족할 경우를 대비하여 exit(0) 호출
//   QMainWindow::closeEvent(event);
//   std::exit(0);
// }

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
                             qintptr *result) {
#else
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message,
                             long *result) {
#endif
  if (m_isInternalChange)
    return false;

  MSG *msg = static_cast<MSG *>(message);
  if (msg->message == WM_NCHITTEST) {
    int x = GET_X_LPARAM(msg->lParam);
    int y = GET_Y_LPARAM(msg->lParam);
    QPoint pt(x, y);
    QPoint localPos = mapFromGlobal(pt);

    if (!rect().contains(localPos))
      return false;

    // [버그 수정] 리사이즈 경계 판정을 Qt의 width()/height()(위젯이 기억하는
    // 논리적 크기)가 아니라 GetWindowRect()로 얻은 실제 네이티브 창 크기로
    // 계산한다. 최대화/복원 토글에서 SetWindowPos()로 직접 크기를 바꾼 직후에는
    // Qt의 캐시가 아주 짧게(다음 WM_SIZE 처리 전까지) 실제 창 크기와 어긋날 수
    // 있는데, 그 사이에 이 경계값이 옛 크기로 계산되면 테두리를 잡아도 리사이즈
    // 커서/드래그가 전혀 반응하지 않는 것처럼 보인다.
    RECT wr;
    ::GetWindowRect(msg->hwnd, &wr);
    int winW = wr.right - wr.left;
    int winH = wr.bottom - wr.top;
    int nx = x - wr.left;
    int ny = y - wr.top;

    // 리사이즈 경계 처리 (30px 타이틀바 영역 위쪽 5px만 허용하거나 전체
    // 사이드)
    int border = 5;
    if (nx < border && ny < border) {
      *result = HTTOPLEFT;
      return true;
    }
    if (nx > winW - border && ny < border) {
      *result = HTTOPRIGHT;
      return true;
    }
    if (nx < border && ny > winH - border) {
      *result = HTBOTTOMLEFT;
      return true;
    }
    if (nx > winW - border && ny > winH - border) {
      *result = HTBOTTOMRIGHT;
      return true;
    }
    if (nx < border) {
      *result = HTLEFT;
      return true;
    }
    if (nx > winW - border) {
      *result = HTRIGHT;
      return true;
    }
    if (ny < border) {
      *result = HTTOP;
      return true;
    }
    if (ny > winH - border) {
      *result = HTBOTTOM;
      return true;
    }

    QWidget *child = childAt(localPos);

    // [수정] MDI 영역 내부 클릭 시 OS 드래그 완전 배제 (Restore 크래시 해결
    // 핵심)
    if (child) {
      QWidget *p = child;
      bool insideMdi = false;
      while (p) {
        if (p == mdiArea) {
          insideMdi = true;
          break;
        }
        p = p->parentWidget();
      }
      if (insideMdi)
        return false;
    }

    // [수정] m_customTitleBar의 툴바인지 확인하여 드래그 이벤트를 막음
    if (child && m_customTitleBar) {
      if (child == m_customTitleBar->toolBar() ||
          child->inherits("QToolButton") || child->inherits("QPushButton")) {
        return false;
      }
    }
    // 메인 타이틀바 영역 (30px)
    if (localPos.y() < 30) {
      *result = HTCAPTION;
      return true;
    }
  }
  return QMainWindow::nativeEvent(eventType, message, result);
}

// [리팩토링] MdiController로 이전됨
void MainWindow::updateSubWindowControls() {
  if (m_mdiController)
    m_mdiController->updateSubWindowControls();

  // [핵심] MDI 영역에 활성 고정창이 숨겨져 있다면 즉시 showMaximized 복원 (회색 바탕 방지)
  if (mdiArea) {
    QMdiSubWindow *activeSub = mdiArea->activeSubWindow();
    if (!activeSub && !mdiArea->subWindowList().isEmpty()) {
      QList<QMdiSubWindow *> subs = mdiArea->subWindowList();
      for (int i = subs.size() - 1; i >= 0; --i) {
        DrawSubWindow *dwCandidate = qobject_cast<DrawSubWindow *>(subs.at(i));
        if (dwCandidate && m_floatController && !m_floatController->isDetached(dwCandidate)) {
          activeSub = dwCandidate;
          mdiArea->setActiveSubWindow(activeSub);
          break;
        }
      }
    }
    if (activeSub) {
      DrawSubWindow *dwActive = qobject_cast<DrawSubWindow *>(activeSub);
      bool isDetached = false;
      if (m_floatController && dwActive) {
        isDetached = m_floatController->isDetached(dwActive);
      }
      if (!isDetached && !activeSub->isVisible()) {
        activeSub->show();
        activeSub->showMaximized();
        activeSub->raise();
        if (dwActive) {
          if (dwActive->widget()) {
            dwActive->widget()->show();
          }
          if (dwActive->view()) {
            dwActive->view()->show();
            dwActive->view()->setCustomCrossCursor();
            if (dwActive->view()->viewport()) {
              dwActive->view()->viewport()->update();
            }
          }
        }
      }
    }
  }

  // [신규] 활성 윈도우에 따른 레이어 테이블/툴바 동기화 (분리 창 우선 지원)
  DrawSubWindow *dw = m_appContext ? m_appContext->activeDrawWindow() : nullptr;
  if (!dw && mdiArea) {
    QMdiSubWindow *activeSub = mdiArea->activeSubWindow();
    dw = qobject_cast<DrawSubWindow *>(activeSub);
  }
  if (dw) {
    if (m_layerTableView) {
      m_layerTableView->setLayerManager(dw->layerManager());
    }
    if (toptoolbar) {
      toptoolbar->updateLayerModel();
    }
    if (m_blockPaletteDock) {
      m_blockPaletteDock->setLayerManager(dw->layerManager());
    }
    if (m_propertyDock) {
      m_propertyDock->setSelectionManager(dw->selectionManager());
    }
    if (myStatusBar && dw->scene()) {
      myStatusBar->updateScale(dw->scene()->drawingScale());
      myStatusBar->updateSize(dw->scene()->drawingSizeName());
      myStatusBar->updateLinearUnit(dw->scene()->linearUnit());
    }
    syncUndoActions(dw->undoStack());
  } else {
    if (m_layerTableView) {
      m_layerTableView->setLayerManager(nullptr);
    }
    if (toptoolbar) {
      toptoolbar->updateLayerModel(); // 내부에서 nullptr 체크 필요
    }
    if (m_blockPaletteDock) {
      m_blockPaletteDock->setLayerManager(nullptr);
    }
    if (m_propertyDock) {
      m_propertyDock->setSelectionManager(nullptr);
    }
    if (myStatusBar) {
      myStatusBar->updateScale(0);
      myStatusBar->updateSize("None");
      myStatusBar->updateLinearUnit(LinearUnit::Mm);
    }
    syncUndoActions(nullptr);
  }

  // [신규] "템플릿으로 저장"은 템플릿 만들기/편집으로 열린 도면에서만 허용
  // (Ctrl+N 로 연 도면은 내용이 템플릿을 따르더라도 저장 불가).
  if (m_appContext) {
    const bool isTemplateOrigin =
        dw && dw->property("isTemplateOrigin").toBool();
    m_appContext->setCommandEnabled(QStringLiteral("menu.file.saveastemplate"),
                                    isTemplateOrigin);
  }

  // [신규] MDI 메인 영역에 열린 창이 없으면 초기화면(대시보드)으로 전환
  if (mdiArea->subWindowList().isEmpty()) {
    showDashboard();
  }
}

bool MainWindow::isEveryMdiSubWindowMinimized() const {
  if (!mdiArea)
    return false;
  QList<QMdiSubWindow *> subs = mdiArea->subWindowList();
  if (subs.isEmpty())
    return false;
  for (auto sub : subs) {
    if (!sub->isMinimized())
      return false;
  }
  return true;
}

void MainWindow::onSubWindowMinimize() {
  if (auto sub = mdiArea->activeSubWindow()) {
    sub->showMinimized();
    // [신규] 탭바에 이 창이 최소화되었음을 표시("_ " 접두어)
    if (docTabWidget)
      docTabWidget->updateTabMinimizedState(sub, true);

    // [버그 수정] 예전에는 최소화되지 않는 다른 서브윈도우들을 강제로
    // Normal 상태로 되돌리고 지오메트리를 재적용했는데, 이로 인해 minimize
    // 대상과 무관한 다른 도면창(예: A)까지 최대화 상태가 풀리며 크기/모양이
    // 함께 바뀌는 부작용이 있었다. minimize는 대상 창에만 영향을 주어야
    // 하므로 다른 창의 상태·지오메트리는 건드리지 않는다.

    // [신규] 모든 서브윈도우가 최소화되어 MDI 영역에 보여줄 도면창이 없으면
    // 회색 배경 대신 대시보드 화면으로 전환한다.
    if (isEveryMdiSubWindowMinimized()) {
      showDashboard();
    } else {
      // [버그 수정] Qt가 다음 활성 창으로 고르는 대상은 내부적으로 창이
      // 열린/정렬된 순서 등에 따라 달라질 수 있어, 실제로 화면에 보이는
      // 도면창과 탭바에서 활성(선택) 색으로 표시되는 탭이 서로 어긋나는
      // 문제가 있었다. 최소화되지 않은 창 중 하나를 명시적으로 활성화해
      // 화면에 보이는 창 = 탭바에서 활성 표시되는 탭이 되도록 맞춘다.
      QList<QMdiSubWindow *> subs = mdiArea->subWindowList();
      QMdiSubWindow *nextActive = nullptr;
      for (int i = subs.size() - 1; i >= 0; --i) {
        if (subs.at(i) != sub && !subs.at(i)->isMinimized()) {
          nextActive = subs.at(i);
          break;
        }
      }
      if (nextActive) {
        nextActive->raise();
        mdiArea->setActiveSubWindow(nextActive);
        // [버그 수정] mdiArea가 최소화 시점에 이미 내부적으로 nextActive를
        // 활성창으로 인식하고 있으면 setActiveSubWindow()가 아무 변화도
        // 없다고 판단해 subWindowActivated 시그널을 다시 emit하지 않는다.
        // 그러면 탭바(docTabWidget)의 currentIndex가 갱신되지 않아, 화면에는
        // nextActive가 보이는데 탭은 여전히 이전(최소화된) 탭이 활성색으로
        // 남는 문제가 생긴다. 시그널에 기대지 않고 탭바를 직접 동기화한다.
        if (docTabWidget)
          docTabWidget->setCurrentDoc(nextActive);

        // [버그 수정] 방금 최소화된 창의 프레임 스타일 동기화(프레임리스<->일반
        // 전환, MdiController의 최대 약 60ms 지연 타이머 체인)가 끝나는
        // 시점에 활성창/탭 선택이 다시 어긋나는 사례가 있어, 그 뒤에 한 번 더
        // 확정적으로 재적용한다.
        QPointer<QMdiSubWindow> guardedNext = nextActive;
        QPointer<QMdiSubWindow> guardedMinimized = sub;
        QTimer::singleShot(150, this, [this, guardedNext, guardedMinimized]() {
          if (!mdiArea || !guardedNext)
            return;
          if (guardedMinimized && !guardedMinimized->isMinimized())
            return; // 그 사이 사용자가 다시 복원했다면 개입하지 않음
          if (mdiArea->activeSubWindow() != guardedNext.data()) {
            guardedNext->raise();
            mdiArea->setActiveSubWindow(guardedNext.data());
          }
          if (docTabWidget)
            docTabWidget->setCurrentDoc(guardedNext.data());
        });
      }
    }

    if (m_mdiController)
      m_mdiController->syncAllSubWindowStyles(); // 컨트롤 버튼 표시 상태 갱신
  }
}

void MainWindow::onSubWindowClose() {
  QPointer<QMdiSubWindow> sub = mdiArea->activeSubWindow();
  if (sub) {
    m_isInternalChange = true;
    sub->close();
    m_isInternalChange = false;
    // updateSubWindowControls()는 subWindowActivated 시그널에 의해 자동으로
    // 호출됨
  }
}

void MainWindow::onCopySelectedToClipboard() {
  if (!m_appContext)
    return;

  MyGraphicsScene *scene = m_appContext->activeScene();
  DrawSubWindow *dw = m_appContext->activeDrawWindow();
  if (!scene || !dw)
    return;

  QList<QGraphicsItem *> selected = scene->selectedItems();
  if (selected.isEmpty())
    return;

  // [핵심] 블록 등의 자식 아이템이 부모와 함께 중복 선택되지 않도록, 다른
  // 편집 도구들과 동일하게 "최상위 CadItem"만 추려낸다.
  QList<CadItem *> topItems;
  if (dw->tool()) {
    topItems = dw->tool()->getTopLevelCadItems(selected);
  } else {
    for (QGraphicsItem *item : selected) {
      if (CadItem *ci = dynamic_cast<CadItem *>(item))
        topItems.append(ci);
    }
  }
  if (topItems.isEmpty())
    return;

  // [핵심] 원본 씬과 완전히 무관한 clone()을 클립보드에 담아, 다른 도면창에
  // 붙여넣거나 원본을 나중에 삭제해도 영향을 받지 않도록 한다.
  QList<CadItem *> clones;
  for (CadItem *item : topItems) {
    if (CadItem *c = item->clone())
      clones.append(c);
  }
  if (clones.isEmpty())
    return;

  m_appContext->setClipboardItems(clones);

  if (m_appContext->commandDock()) {
    m_appContext->commandDock()->logMessage(
        tr("%1개 객체를 복사했습니다.").arg(clones.size()));
  }
}

void MainWindow::onPasteFromClipboard() {
  if (!m_appContext)
    return;

  MyGraphicsScene *scene = m_appContext->activeScene();
  if (!scene)
    return;

  QList<CadItem *> clipboard = m_appContext->clipboardItems();
  if (clipboard.isEmpty())
    return;

  // [핵심] 대상 도면창(현재 활성창)의 실행취소 스택에 기록해, 붙여넣기도
  // 다른 편집 명령처럼 Undo/Redo가 가능하게 한다.
  QUndoStack *undo = m_appContext->undoStack();
  if (undo)
    undo->beginMacro(tr("붙여넣기"));

  scene->clearSelection();
  int pastedCount = 0;
  for (CadItem *src : std::as_const(clipboard)) {
    if (!src)
      continue;
    // [핵심] 클립보드 원본이 아니라 매번 새로 clone()하여, 여러 번
    // 붙여넣어도 서로 독립적인 별개의 객체가 생성되도록 한다.
    CadItem *newItem = src->clone();
    if (!newItem)
      continue;

    if (undo) {
      undo->push(new AddEntityCommand(scene, newItem));
    } else {
      QGraphicsItem *root = scene->getLayerRoot(newItem->layerName());
      newItem->setParentItem(root);
    }
    newItem->setSelected(true);
    ++pastedCount;
  }

  if (undo)
    undo->endMacro();

  if (pastedCount > 0 && m_appContext->commandDock()) {
    m_appContext->commandDock()->logMessage(
        tr("%1개 객체를 붙여넣었습니다.").arg(pastedCount));
  }
}

void MainWindow::onSubWindowFloatToggle() {
  if (!m_floatController || !m_appContext)
    return;

  DrawSubWindow *activeSub = m_appContext->activeDrawWindow();
  if (!activeSub) {
    activeSub = qobject_cast<DrawSubWindow *>(mdiArea->activeSubWindow());
  }

  if (activeSub) {
    m_floatController->toggleFloat(activeSub);
  }
}

void MainWindow::onTabFloatWindowRequested(QMdiSubWindow *subWindow) {
  if (!m_floatController || !subWindow)
    return;

  DrawSubWindow *drawSub = qobject_cast<DrawSubWindow *>(subWindow);
  if (drawSub) {
    m_floatController->toggleFloat(drawSub);
  }
}

void MainWindow::onSubWindowDetached(DrawSubWindow *subWindow) {
  if (m_floatController && subWindow) {
    m_floatController->setActiveFloatingWindow(subWindow);
  }
  if (docTabWidget && subWindow) {
    docTabWidget->updateTabFloatState(subWindow, true);
    docTabWidget->setCurrentDoc(subWindow);
  }
  if (mdiArea && mdiArea->subWindowList().isEmpty()) {
    showDashboard();
  } else if (mdiArea) {
    showWorkspace();
    QMdiSubWindow *nextActive = mdiArea->activeSubWindow();
    if (!nextActive || (m_floatController && m_floatController->isDetached(qobject_cast<DrawSubWindow *>(nextActive)))) {
      QList<QMdiSubWindow *> subs = mdiArea->subWindowList();
      for (int i = subs.size() - 1; i >= 0; --i) {
        DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(subs.at(i));
        if (dw && m_floatController && !m_floatController->isDetached(dw)) {
          nextActive = dw;
          break;
        }
      }
    }
    if (nextActive) {
      nextActive->show();
      nextActive->showMaximized();
      nextActive->raise();
      if (DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(nextActive)) {
        if (dw->widget()) {
          dw->widget()->show();
        }
        if (dw->view()) {
          dw->view()->show();
          dw->view()->setCustomCrossCursor();
        }
      }
      mdiArea->setActiveSubWindow(nextActive);
    }
  }
  updateSubWindowControls();
}

void MainWindow::onSubWindowAttached(DrawSubWindow *subWindow) {
  if (m_floatController) {
    m_floatController->clearActiveFloatingWindow();
  }
  showWorkspace(); // [신규] MDI 복귀 시 대시보드에서 작업 영역으로 즉시 전환
  if (subWindow) {
    subWindow->installEventFilter(this);
    if (mdiArea) {
      mdiArea->setActiveSubWindow(subWindow);
      QRect mdiRect = mdiArea->viewport()->rect();
      if (mdiRect.isValid()) {
        subWindow->setGeometry(mdiRect);
      }
    }
    subWindow->showMaximized();
    m_pendingRestoreSubWindow = subWindow;
    // 사용자 규칙 준수: 람다 대신 멤버 함수 포인터 사용
    // 100ms 단일 타이머를 통해 MDI 영역 레이아웃 확정 후 탭 클릭 복원 트리거
    QTimer::singleShot(100, this, &MainWindow::onSubWindowAttachRestoreTimeout);
  }
  if (docTabWidget && subWindow) {
    docTabWidget->updateTabFloatState(subWindow, false);
  }
  updateSubWindowControls();
}

void MainWindow::onSubWindowFloatingActivated(DrawSubWindow *subWindow) {
  if (!subWindow)
    return;

  if (m_floatController && m_floatController->isDetached(subWindow)) {
    m_floatController->setActiveFloatingWindow(subWindow);
  } else {
    if (m_floatController) {
      m_floatController->clearActiveFloatingWindow();
    }
    showWorkspace();
    if (mdiArea) {
      if (mdiArea->activeSubWindow() != subWindow) {
        mdiArea->setActiveSubWindow(subWindow);
      }
      subWindow->showMaximized();
      subWindow->raise();
    }
    if (subWindow->view()) {
      subWindow->view()->setFocus();
      subWindow->view()->setCustomCrossCursor();
      if (subWindow->view()->viewport()) {
        subWindow->view()->viewport()->update();
      }
    }
  }

  if (docTabWidget) {
    docTabWidget->setCurrentDoc(subWindow);
  }
  updateSubWindowControls();
}

void MainWindow::showDashboard() {
  m_centralStack->setCurrentWidget(m_dashboard);
  m_dashboard->refreshRecentFiles();
  m_dashboard->refreshTemplateCombo(); // [신규] 기본 원형도면 콤보 동기화
  // [신규] "템플릿 보기"는 대시보드(초기화면)에서만 사용 가능
  if (m_appContext)
    m_appContext->setCommandEnabled(QStringLiteral("menu.file.newfromtemplate"),
                                    true);
}

void MainWindow::showWorkspace() {
  // [버그 수정] mdiArea는 이제 리사이즈 테두리 침범 방지용 여백 래퍼 위젯 안에
  // 들어있다(생성자의 mdiAreaWrapper 참고) — m_centralStack의 실제 페이지는 그
  // 래퍼이므로, mdiArea의 부모(래퍼)를 페이지로 전환한다.
  m_centralStack->setCurrentWidget(mdiArea->parentWidget());
  // [신규] 작업 영역으로 전환되면 "템플릿 보기"를 잠근다.
  if (m_appContext)
    m_appContext->setCommandEnabled(QStringLiteral("menu.file.newfromtemplate"),
                                    false);
  updateSubWindowControls();

  if (mdiArea) {
    if (QMdiSubWindow *sub = mdiArea->activeSubWindow()) {
      sub->activateWindow();
      if (DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(sub)) {
        if (dw->view()) {
          dw->view()->setFocus();
        }
      }
    }
  }
}

// ==========================================================
// [수정된 Undo/Redo 동기화]
// m_customTitleBar 내부의 액션 포인터를 가져와서 설정합니다.
// ==========================================================
void MainWindow::syncUndoActions(QUndoStack *stack) {
  if (!m_customTitleBar)
    return;

  QAction *undoAct = m_customTitleBar->undoAction();
  QAction *redoAct = m_customTitleBar->redoAction();

  if (!undoAct || !redoAct)
    return;

  undoAct->disconnect();
  redoAct->disconnect();

  if (stack) {
    connect(undoAct, &QAction::triggered, stack, &QUndoStack::undo);
    connect(redoAct, &QAction::triggered, stack, &QUndoStack::redo);

    connect(stack, &QUndoStack::canUndoChanged, undoAct, &QAction::setEnabled);
    connect(stack, &QUndoStack::canRedoChanged, redoAct, &QAction::setEnabled);

    undoAct->setEnabled(stack->canUndo());
    redoAct->setEnabled(stack->canRedo());
  } else {
    undoAct->setEnabled(false);
    redoAct->setEnabled(false);
  }
}

void MainWindow::onCurrentFontChanged(const QString &family, qreal size) {
  Q_UNUSED(size);
  MyGraphicsScene *scene = m_appContext->activeScene();
  if (scene) {
    DimensionStyle style = scene->dimensionStyle();
    style.fontFamily = family;
    scene->setDimensionStyle(style);
  }
}

// ==========================================================
// [신규] QMdiArea 배경 색상 갱신
// QMdiArea는 CSS background-color를 무시하므로 setBackground()로 직접 설정.
// 서브윈도우가 없거나 최대화되지 않은 경우 MDI 영역 빈 공간에 적용됩니다.
// ==========================================================
void MainWindow::onMdiThemeChanged(ThemeManager::ThemeType type) {
  Q_UNUSED(type)
  if (mdiArea && m_appContext && m_appContext->themeManager()) {
    ThemeManager *tm = m_appContext->themeManager();
    mdiArea->setBackground(QBrush(tm->windowBackground()));
  }
}

void MainWindow::restoreFocusToActiveView() {
  if (!m_pendingFocusWindow)
    return;

  DrawSubWindow *dw = m_pendingFocusWindow.data();
  if (!dw)
    return;

  bool isFloating = (m_floatController && m_floatController->isDetached(dw)) ||
                    (dw->parentWidget() == nullptr);
  if (isFloating) {
    dw->activateWindow();
    dw->raise();
  } else if (mdiArea) {
    mdiArea->setActiveSubWindow(dw);
  }

  if (dw->view()) {
    dw->view()->setFocus();
    dw->view()->setCustomCrossCursor();
  }
}

void MainWindow::onNewWindowRequested() {
  if (cmdManager) {
    cmdManager->executeStringCommand("new");
  }
  showWorkspace();
}

void MainWindow::onSubWindowAttachRestoreTimeout() {
  if (!m_pendingRestoreSubWindow || !mdiArea)
    return;

  DrawSubWindow *dw = m_pendingRestoreSubWindow.data();
  if (!dw)
    return;

  // 도킹된 창의 내부 변경 상태 설정 (이벤트 오작동 차단)
  dw->setProperty("isInternalChanging", true);

  QRect mdiRect = mdiArea->viewport()->rect();
  if (mdiRect.isValid()) {
    dw->setGeometry(mdiRect);
  }

  mdiArea->setActiveSubWindow(dw);
  dw->show();
  dw->showMaximized();

  if (mdiRect.isValid()) {
    dw->setGeometry(mdiRect);
  }
  dw->raise();
  dw->activateWindow();

  if (dw->widget()) {
    dw->widget()->show();
  }

  updateSubWindowControls();

  if (dw->view()) {
    dw->view()->show();
    dw->view()->setFocus();
    dw->view()->setCustomCrossCursor();
    dw->view()->zoomAll();
    dw->view()->update();
    if (dw->view()->viewport()) {
      dw->view()->viewport()->update();
      dw->view()->viewport()->repaint();
    }
    dw->view()->repaint();
  }

  if (dw->scene()) {
    dw->scene()->update();
  }

  dw->setProperty("isInternalChanging", false);
  dw->setProperty("isDockingAttached", false);

  if (docTabWidget) {
    docTabWidget->clickDocTab(dw);
  }
}

void MainWindow::openDrawingFile(const QString &path) {
  FileOpenCommand *openCmd =
      qobject_cast<FileOpenCommand *>(cmdManager->findCommand("open"));
  if (!openCmd)
    return;

  // [보강] 도면 열기 진입 전 혹시 남아있을 수 있는 마우스 그래버 및 OS 캡처 즉시 해제
  if (QWidget::mouseGrabber()) {
    QWidget::mouseGrabber()->releaseMouse();
  }
#ifdef Q_OS_WIN
  if (::GetCapture()) {
    ::ReleaseCapture();
  }
#endif

  QProgressDialog progress(tr("도면을 불러오는 중입니다..."), QString(), 0, 0,
                           this);
  progress.setWindowTitle(tr("도면 열기"));
  // [버그 수정] Qt::WindowModal로 띄우면 이 다이얼로그가 프레임리스 소유자 창
  // (MainWindow)과 별도의 네이티브 모달 창 관계를 맺는데, 이게 닫힌 뒤에도
  // MainWindow의 최대화/복원 토글이 반응하지 않는 문제가 생기므로 NonModal로 띄운다.
  progress.setWindowModality(Qt::NonModal);
  progress.setMinimumDuration(0);
  progress.setCancelButton(nullptr);
  progress.setValue(0);
  progress.show();
  QApplication::processEvents();
  QApplication::setOverrideCursor(Qt::WaitCursor);

  openCmd->setFilePath(path);
  openCmd->setProgressDialog(&progress);
  openCmd->execute();
  showWorkspace();
  // 작업 영역 전환으로 발생한 첫 렌더링(대량 아이템)을 다이얼로그가 떠 있는 동안 처리
  QApplication::processEvents();

  QApplication::restoreOverrideCursor();
  progress.close();
  progress.reset();

  // [핵심] 더블클릭 및 다이얼로그 종료 후 혹시 남아있을 수 있는 마우스 그래버 및 OS 캡처 강제 해제
  if (QWidget::mouseGrabber()) {
    QWidget::mouseGrabber()->releaseMouse();
  }
#ifdef Q_OS_WIN
  if (::GetCapture()) {
    ::ReleaseCapture();
  }
#endif

  QMdiSubWindow *targetSub = mdiArea ? mdiArea->activeSubWindow() : nullptr;

  // [핵심] + 클릭 시와 동일하게 탭 활성화 및 뷰포트 렌더링 트리거
  if (docTabWidget && targetSub) {
    docTabWidget->clickDocTab(targetSub);
  }

  // [핵심] 진행 다이얼로그 소멸 및 OS 윈도우 정리 완료 시점(100ms)에 MdiController를 통해 완벽한 OS 활성화 동기화
  if (m_mdiController && targetSub) {
    m_mdiController->scheduleSubWindowActivation(targetSub, 100);
  }
}

