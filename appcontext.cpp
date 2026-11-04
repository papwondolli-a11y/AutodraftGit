#include "appcontext.h"
#include <QCoreApplication>
#include <QDir>
#include "abstracttool.h"
#include "caditem.h"
#include "commanddock.h"
#include "customstatusbar.h"
#include "drawsubwindow.h"
#include "entityfactory.h"
#include "filemanager.h"
#include "inputparser.h" // [수정] std::make_unique 사용을 위해 전체 정의 필요
#include "layermanager.h"
#include "linetypemanager.h"
#include "mainwindow.h"
#include "mygraphicsscene.h"
#include "mygraphicsview.h"
#include "resourcemanager.h"
#include "selectionmanager.h"
#include "selecttool.h"
#include "snapmanager.h"
#include "stylemanager.h"
#include "subwindowfloatcontroller.h"
#include "thememanager.h"

#include <QApplication>
#include <QDebug>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QCoreApplication>
#include <QFile>

AppContext::AppContext(QMdiArea *mdiArea, CustomStatusBar *statusBar,
                       CommandDock *commandDock, QObject *parent)
    : QObject(parent), m_mdiArea(mdiArea), m_statusBar(statusBar),
      m_commandDock(commandDock), m_lastActiveWindow(nullptr),
      m_currentFontFamily("Arial") ,
      m_currentFontSize(2.5), // 기본 크기
      m_textLayerName("0")     // 기본 텍스트 레이어 (기본 레이어로 초기화)
{
  // 1. 스냅 매니저 및 파서 생성 (이중 해제 방지를 위해 부모를 nullptr로 설정)
  m_snapManager = std::make_unique<SnapManager>(nullptr);
  m_selectionManager = std::make_unique<SelectionManager>(nullptr);
  m_entityFactory = std::make_unique<EntityFactory>(nullptr);
  m_inputParser = std::make_unique<InputParser>();

  // 2. 기본 선택 툴 생성 (역시 부모 없이 unique_ptr가 소유)
  m_selectTool = std::make_unique<SelectTool>(nullptr);
  m_selectTool->setContext(this);

  m_lineTypeManager = std::make_unique<LineTypeManager>();
  m_styleManager = std::make_unique<StyleManager>();
  m_themeManager = std::make_unique<ThemeManager>();
  m_resourceManager = std::make_unique<ResourceManager>();
  m_layerManager = std::make_unique<LayerManager>(this);
  m_undoStack = std::make_unique<QUndoStack>(this);
  m_fileManager = std::make_unique<FileManager>(this);

  // =========================================================
  // [추가] HatchPatternManager 초기화 및 패턴 파일 로드
  // =========================================================
  m_hatchPatternManager = std::make_unique<HatchPatternManager>();

  // 1순위: 배포 환경 — 실행 파일(.exe) 옆 Data 폴더
  QString patFilePath = QCoreApplication::applicationDirPath() + "/Data/hatchiso.pat";
  // 2순위: qmake 빌드 — exe가 release/ 또는 debug/ 하위일 때 한 단계 위 확인
  if (!QFile::exists(patFilePath)) {
      patFilePath = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../Data/hatchiso.pat");
  }
  // 3순위: CMake 개발 환경 — APP_SOURCE_PATH(소스 루트)에서 탐색
#ifdef APP_SOURCE_PATH
  if (!QFile::exists(patFilePath)) {
      patFilePath = QString(APP_SOURCE_PATH) + "/Data/hatchiso.pat";
  }
#endif

  m_hatchPatternManager->loadPatterns(patFilePath);

  // 전역 포커스 변경 감지 (멤버 함수 포인터로 연결)
  connect(qApp, &QApplication::focusChanged, this,
          &AppContext::onApplicationFocusChanged);

  // [신규] LayerManager 시그널 연결
  connect(m_layerManager.get(), &LayerManager::currentLayerChanged, this,
          &AppContext::currentLayerChanged);
  connect(m_layerManager.get(), &LayerManager::layerPropertyChanged, this,
          &AppContext::onLayerPropertyChanged);

  // 3. 기본 속성 초기화
  m_defaultPen = QPen(Qt::black);
  m_defaultPen.setCosmetic(true);
  m_defaultBrush = QBrush(Qt::NoBrush);
}

AppContext::~AppContext() {
  // std::unique_ptr가 자동으로 메모리를 해제하므로 명시적 delete가 필요 없음
  qDeleteAll(m_clipboardItems); // [신규] 클립보드가 소유한 clone 항목 해제
}

// [신규] 도면창 간 객체 복사/붙여넣기용 클립보드
void AppContext::setClipboardItems(const QList<CadItem *> &items) {
  if (&items == &m_clipboardItems)
    return;
  qDeleteAll(m_clipboardItems); // 이전에 복사해뒀던 항목은 소유권을 가지므로 해제
  m_clipboardItems = items;
}

QList<CadItem *> AppContext::clipboardItems() const { return m_clipboardItems; }

// [IAppContext 구현] CommandDock 반환
CommandDock *AppContext::commandDock() const { return m_commandDock; }

void AppContext::setLastActiveWindow(DrawSubWindow *w) {
  if (m_lastActiveWindow.data() != w) {
    m_lastActiveWindow = w;
  }
}

void AppContext::onApplicationFocusChanged(QWidget *old, QWidget *now) {
  Q_UNUSED(old);
  if (!now)
    return;

  // 포커스를 얻은 위젯의 부모 트리를 거슬러 올라가며 DrawSubWindow 탐색
  QWidget *w = now;
  while (w) {
    if (DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(w)) {
      setLastActiveWindow(dw);
      return;
    }
    w = w->parentWidget();
  }
}

DrawSubWindow *AppContext::activeDrawWindow() const {
  // 0순위: 분리되어 떠 있는 플로팅 윈도우 중 현재 활성화된 창이 있는 경우 최우선 반환
  // (커맨드라인 포커스로 인해 QMdiArea가 부착창을 자동 활성화하더라도 분리 창의 작업을 가로채지 못하도록 보호)
  if (m_floatController && m_floatController->activeFloatingWindow()) {
    DrawSubWindow *fw = m_floatController->activeFloatingWindow();
    if (fw && m_floatController->isDetached(fw)) {
      return fw;
    }
  }

  // 1순위: 가장 최근에 활성화/작업된 도면 창 (분리 창 또는 MDI 창)
  if (m_lastActiveWindow) {
    return m_lastActiveWindow.data();
  }

  // 2순위: MDI 영역의 현재 활성 서브윈도우
  if (m_mdiArea) {
    QMdiSubWindow *sub = m_mdiArea->currentSubWindow();
    if (sub) {
      DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(sub);
      if (dw) {
        return dw;
      }
    }

    // 3순위: MDI 서브윈도우 목록 순회 (C++11 detach 경고 방지 인덱스 루프)
    QList<QMdiSubWindow *> list = m_mdiArea->subWindowList();
    for (int i = 0; i < list.size(); ++i) {
      DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(list.at(i));
      if (dw) {
        return dw;
      }
    }
  }

  return nullptr;
}

void AppContext::setCommandEnabled(const QString &id, bool enabled) {
  emit requestSetCommandEnabled(id, enabled);
}

void AppContext::refreshDashboardTemplates() {
  emit requestRefreshDashboardTemplates();
}

void AppContext::addSubWindow(DrawSubWindow *window) {
  if (!window)
    return;

  // [핵심] 컨텍스트 주입 (focusInEvent 크래시 방지)
  window->setContext(this);

  // [핵심] 새 도면 창을 즉시 최근 활성 창으로 등록
  setLastActiveWindow(window);

  // [핵심] 새 윈도우가 생성되면, 공유된 SelectTool을 기본 도구로 등록
  if (m_selectTool) {
    window->setDefaultTool(m_selectTool.get()); // [수정] .get() 사용
  } else {
 //   qDebug() << "Critical Error: AppContext has no SelectTool instance!";
  }

  // MainWindow에게 추가 요청 (시그널 발행)
  emit requestAddSubWindow(window);
}

MyGraphicsScene *AppContext::activeScene() const {
  DrawSubWindow *w = activeDrawWindow();
  return w ? w->scene() : nullptr;
}

MyGraphicsView *AppContext::activeView() const {
  DrawSubWindow *w = activeDrawWindow();
  return w ? w->view() : nullptr;
}

InputParser *AppContext::parser() const {
  // [Phase 3] 활성 도면 단위(mm/inch)를 InputParser 환산 배율로 반영.
  //   씬이 없으면(도면 미열림) 기본 mm(1.0).
  const MyGraphicsScene *sc = activeScene();
  m_inputParser->setUnitScale(
      sc ? UnitConvert::mmPerUnit(sc->linearUnit()) : 1.0);
  return m_inputParser.get();
}

// =========================================================
// 2. 도구 제어 구현
// =========================================================

void AppContext::startTool(AbstractTool *tool) {
  if (!tool)
    return;

  // [핵심 추가] 툴에게 AppContext(나 자신)를 주입해 줍니다.
  tool->setContext(this);

  DrawSubWindow *dw = activeDrawWindow();

  if (dw) {
    // 도면 윈도우에 새 도구 장착 (기존 도구는 DrawSubWindow 내부에서 정리됨)
    dw->setTool(tool);

    // [신규] 툴 장착 후 대상 도면 창 활성화 및 십자선 커서 준비 (포커스는 호출처에서 비동기 처리하여 Double Enter 방지)
    if (dw->parentWidget() == nullptr) {
      dw->activateWindow();
      dw->raise();
    } else if (m_mdiArea) {
      m_mdiArea->setActiveSubWindow(dw);
    }
    if (dw->view()) {
      dw->view()->setCustomCrossCursor();
    }
  } else {
    // 활성 도면이 없으면 도구를 사용할 수 없으므로 삭제 (메모리 누수 방지)
    delete tool;
    if (commandDock()) {
      commandDock()->logMessage("No active drawing. Tool canceled.",
                                CommandDock::Warning);
    } else {
  //    qDebug() << "No active drawing. Tool canceled.";
    }
  }
}

void AppContext::finishCurrentTool() {
  DrawSubWindow *w = activeDrawWindow();
  if (w) {
    // 현재 도구를 끝내고 기본 도구(SelectTool)로 복귀
    w->resetToDefaultTool();
    showStatusText("Ready.");
  }
}

LayerManager *AppContext::layerManager() const {
  DrawSubWindow *w = activeDrawWindow();
  if (w) {
    return w->layerManager();
  }
  return m_layerManager.get();
}

QUndoStack *AppContext::undoStack() const {
  DrawSubWindow *w = activeDrawWindow();
  if (w) {
    return w->undoStack();
  }
  return m_undoStack.get();
}

SelectionManager *AppContext::selectionManager() const {
  DrawSubWindow *w = activeDrawWindow();
  if (w) {
    return w->selectionManager();
  }
  return m_selectionManager.get();
}

// =========================================================
// 3. 전역 속성 구현
// =========================================================

QPen AppContext::currentPen() const {
  // 추후 속성 패널(Property Panel)과 연동 가능
  return m_defaultPen;
}

QBrush AppContext::currentBrush() const { return m_defaultBrush; }

// =========================================================
// 4. 사용자 피드백 구현
// =========================================================

void AppContext::showStatusText(const QString &text) {
  if (m_statusBar) {
    m_statusBar->showMessage(text, 3000); // 3초간 표시
  }
}

void AppContext::updateLayerVisibility(const QString &layerName,
                                       bool isVisible) {
  DrawSubWindow *dw = activeDrawWindow();
  if (dw && dw->scene()) {
    dw->scene()->setLayerVisible(layerName, isVisible);
  }

  // [신규] 상태 변경 알림 시그널 발행
  emit currentLayerChanged(layerName);
}

void AppContext::toggleLineWeight(bool show) {
  if (!m_mdiArea)
    return;

  QList<QMdiSubWindow *> subWindows = m_mdiArea->subWindowList();

  // 열려있는 모든 창에 선가중치 표시 명령을 내립니다.
  for (int i = 0; i < subWindows.size(); ++i) {
    DrawSubWindow *dw = qobject_cast<DrawSubWindow *>(subWindows.at(i));
    if (dw && dw->scene()) {
      dw->scene()->setShowLineWeight(show);
    }
  }
}

// =========================================================
// [신규] 폰트 설정 Getter / Setter 구현
// =========================================================

void AppContext::setCurrentFontFamily(const QString &family) {
  if (m_currentFontFamily != family) {
    m_currentFontFamily = family;
    emit currentFontChanged(m_currentFontFamily, m_currentFontSize);
  }
}

QString AppContext::currentFontFamily() const { return m_currentFontFamily; }

void AppContext::setCurrentFontSize(qreal size) {
  if (!qFuzzyCompare(m_currentFontSize, size)) {
    m_currentFontSize = size;
    emit currentFontChanged(m_currentFontFamily, m_currentFontSize);
  }
}

qreal AppContext::currentFontSize() const { return m_currentFontSize; }

void AppContext::setCurrentFontStyle(bool bold, bool italic, bool underline) {
  if (m_currentFontBold == bold && m_currentFontItalic == italic &&
      m_currentFontUnderline == underline)
    return;
  m_currentFontBold = bold;
  m_currentFontItalic = italic;
  m_currentFontUnderline = underline;
  emit currentFontStyleChanged(bold, italic, underline);
  emit currentFontChanged(m_currentFontFamily, m_currentFontSize);
}

void AppContext::setTextLayerName(const QString &layerName) {
  if (m_textLayerName != layerName) {
    m_textLayerName = layerName;
    emit textLayerChanged(m_textLayerName);
  }
}

QString AppContext::textLayerName() const { return m_textLayerName; }

void AppContext::addToRecentFiles(const QString &filePath) {
  emit recentFileAdded(filePath);
}

ThemeManager *AppContext::themeManager() const { return m_themeManager.get(); }
ResourceManager *AppContext::resourceManager() const {
  return m_resourceManager.get();
}

void AppContext::onLayerPropertyChanged(const QString &layerName) {
  // 전역 레이어 매니저 속성이 바뀌었을 때 (활성 창이 없을 때 등 fallback 용)
  // 현재는 각 DrawSubWindow가 자신의 LayerManager 신호를 직접 관리하게 설계
  // 변경됨. 이 함수는 필요 시 전역 UI 갱신용으로만 남겨둡니다.
  emit layerPropertyChanged(layerName);
}

void AppContext::showBlockPalette() { emit requestShowBlockPalette(); }


void AppContext::activateBlockLibrary(const QString &subFolder) {
  // 배포: 실행파일과 같은 폴더에 blocks/ 존재
  // 개발(qmake): 실행파일은 release/ 또는 debug/ 하위, blocks/는 한 단계 위
  // 개발(Qt Creator "Run", CMake 킷): 실행파일이 예를 들어
  //   build/Desktop_Qt_6_11_0_MinGW_64_bit_Debug/debug/ 처럼 프로젝트 루트보다
  //   여러 단계 아래 섀도 빌드 폴더에 생긴다. 예전에는 딱 한 단계 위("..")만
  //   확인해서 이 경우 blocks/ 폴더를 못 찾았고, 그러면 존재하지 않는 첫 후보
  //   경로가 그대로 emit되어 — 폴더 콤보에는 폴더 "이름"(경로 마지막 조각)만
  //   표시되고 실제 QDir::exists() 검사에는 걸려 목록은 빈 채로 남았다.
  //   실행파일 위치에서 최대 5단계까지 부모 폴더를 올라가며 blocks/subFolder를
  //   찾아 이 케이스까지 커버한다.
  QDir dir(QCoreApplication::applicationDirPath());
  QString path = dir.filePath("blocks/" + subFolder);
  for (int up = 0; up < 5 && !QDir(path).exists(); ++up) {
    if (!dir.cdUp())
      break;
    path = dir.filePath("blocks/" + subFolder);
  }
  emit requestActivateBlockLibrary(QDir::cleanPath(path));
}
