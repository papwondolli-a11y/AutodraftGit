#include "thememanager.h"
#include <QApplication>
#include <QMap>
#include <QPalette>
#include <QSettings>
#include <QStyle>
#include <QStyleFactory>
#include <algorithm>

// 테마 값을 저장/불러오는 QSettings 그룹/키. 저장소는 인자 없는 QSettings()
// (= main() 에서 지정한 정규 .ini)를 쓴다.
static const char *kSettingsGroup = "Theme";
static const char *kSettingsKey = "CurrentTheme";

ThemeManager::ThemeManager(QObject *parent)
    : QObject(parent), m_currentTheme(ThemeType::CADClassic) {
  // 프로그램 시작 시 마지막으로 저장된 테마 값을 불러와 적용한다.
  QSettings settings;
  settings.beginGroup(kSettingsGroup);
  int savedTheme = settings.value(kSettingsKey,
                                   static_cast<int>(ThemeType::CADClassic))
                        .toInt();
  settings.endGroup();

  if (savedTheme == static_cast<int>(ThemeType::Light) ||
      savedTheme == static_cast<int>(ThemeType::Dark) ||
      savedTheme == static_cast<int>(ThemeType::CADClassic)) {
    m_currentTheme = static_cast<ThemeType>(savedTheme);
  }

  applyTheme();
}

void ThemeManager::setTheme(ThemeType type) {
  if (m_currentTheme != type) {
    emit themeAboutToChange(); // [추가] 기존 크기가 망가지기 전에 현재 상태를 저장하라고 알림

    m_currentTheme = type;
    applyTheme();

    // 다음 실행 시에도 동일한 테마가 적용되도록 값을 저장한다.
    QSettings settings;
    settings.beginGroup(kSettingsGroup);
    settings.setValue(kSettingsKey, static_cast<int>(m_currentTheme));
    settings.endGroup();

    emit themeChanged(m_currentTheme);
  }
}

QColor ThemeManager::canvasBackground() const {
  switch (m_currentTheme) {
  case ThemeType::Dark:
    return QColor(0x18, 0x18, 0x18);     // "#181818fb"
  case ThemeType::CADClassic:
    return QColor(220, 220, 220);   // "#f7f7f7f6"
  case ThemeType::Light:
  default:
    return QColor(0xf7, 0xf7, 0xf6); // "#f7f7f7f6"
  }
}

// CSS @WinBg 와 동일한 값 - QMdiArea::setBackground() 등 코드에서 사용
QColor ThemeManager::windowBackground() const {
  switch (m_currentTheme) {
  case ThemeType::Dark:
  case ThemeType::CADClassic:
    return QColor(0xc0, 0xc0, 0xc0); // "#ffffffee"
  case ThemeType::Light:
  default:
    return QColor(0xc0, 0xc0, 0xc0); // "#cececeff"
  }
}

// QColor ThemeManager::gridColor() const {
//   switch (m_currentTheme) {
//   case ThemeType::Dark:
//   case ThemeType::CADClassic:
//     return QColor(220, 220, 220);
//   case ThemeType::Light:
//   default:
//     return QColor(220, 220, 220);
//   }
// }

QColor ThemeManager::ghostColor() const {
  switch (m_currentTheme) {
  case ThemeType::Dark:
    return QColor(200, 200, 200, 200); // 밝은 회색, 다크 캔버스에서 잘 보임
  case ThemeType::CADClassic:
  case ThemeType::Light:
  default:
    return QColor(60, 60, 60, 200);   // 어두운 회색, 밝은 캔버스에서 잘 보임
  }
}
// ==========================================================
// [템플릿 조각 1] 기타 기본 위젯 템플릿 (옵션)
// ==========================================================
QString ThemeManager::getBaseWidgetTemplate() const {
  return R"(
    QMainWindow { border: 1px solid @cDocTS; spacing : 0px; }
    QMainWindow::separator {
    background-color: @cDocTS; /* 구분선 색상 */
    width: 1px;               /* 세로 구분선의 두께 (중앙-도크 사이) */
    height: 1px;              /* 가로 구분선의 두께 (상하 도크 사이) */
       }

QMainWindow QTabBar {
    background-color: @qMtabBg;
    border: none;
} 
QMainWindow QTabBar::tab {
    background-color: @qMtabBg;
    color: @qMtabText;
    border: 1px solid @Border; 
    border-top: none;
    padding: 6px 10px; 
    min-width: 10px;
}
QMainWindow QTabBar::tab:selected {
    background-color: @qMtoolBg;
    color: @qMtoolText;
  /*  border-bottom: 2px solid @Accent;  */
    font-weight: bold;
}
QMainWindow QTabBar::tab:hover:!selected {
    color: @qMtoolText;
    background-color: @upHover;
}      
QToolBar {border: none; spacing: 0px; }
QMdiArea { border: 1px solid @qMdiBg; spacing : 0px; 
                    border-top: 2px solid @qMdiBg;} 
QStatusBar { background: @qStatuBg; color: @qStatuText; border: none; }
  )";
}
// ==========================================================
// [템플릿 조각 2] CustomTitleBar 전용 템플릿
// ==========================================================
QString ThemeManager::getTitleBarTemplate() const {
  return R"(
    CustomTitleBar {
      background-color: @cTitleBg;
      border : none;
    }
    CustomTitleBar QLabel#MainTitleLabel {
      font-weight: bold; font-family: 'Segoe UI', sans-serif;
      font-size: 13px; color: @cTitleText;
    }
    CustomTitleBar QToolBar {
      border: none; background: transparent; spacing: 5px;
    }
    CustomTitleBar QToolBar QToolButton:hover {
      background-color: @cTitleHover ;}

    CustomTitleBar QPushButton {
      background: transparent ; border: none; border-radius: 2px; padding: 4px;
     color : @cTitleText;
     }

    CustomTitleBar QPushButton ::hover{
      background-color: @cTitleHover ;}

    CustomTitleBar QPushButton:hover {
      background-color: @cTitleHover; border-radius: 0px;
    }
    CustomTitleBar QPushButton#TitleCloseButton:hover {
      background-color: #e81123; color: white; border-radius: 0px;
    }
    CustomTitleBar QPushButton#TitleAiButton {
      font-weight: bold; padding: 0px 6px;
    }
    CustomTitleBar QPushButton#TitleAiButton:checked {
      background-color: #2e9d4f; color: white; border-radius: 3px;
    }
    CustomTitleBar QPushButton#TitleAiButton:checked:hover {
      background-color: #3dbd63; color: white; border-radius: 3px;
    }
  )";
}

// ==========================================================
// [템플릿 조각 3] CustomMenuBar 전용 템플릿
// ==========================================================
QString ThemeManager::getMenuBarTemplate() const {
  return R"(
    CustomMenuBar { background-color: @cMenuBg; border : none; }
    CustomMenuBar::item { background-color: transparent; color: @cMenuText; padding: 4px 10px; }
    CustomMenuBar::item:selected { background-color: @cMenuSText;}
    CustomMenuBar QMenu { background-color: @cMenuBg; color: @cMenuText; border: 1px solid @Border; }
    CustomMenuBar QMenu::item { padding: 5px 25px 5px 20px; }
    CustomMenuBar QMenu::item:selected { background-color: @cMenuSText; color: @cTitleText; }
  )";
}

// ==========================================================
// [템플릿 조각 3] CustomTabWidget 전용 템플릿
// ==========================================================
QString ThemeManager::getTabWidgetTemplate() const {
  return R"(
    CustomTabWidget { background-color: @cTabBg !important; border : none; }
    CustomTabWidget::pane { border: none; top: 0px; }
    CustomTabWidget QTabBar::tab {
      background: @cTabBg; border: 1px solid @Border; color: @cTabSText;
      margin: 0px; padding: 4px 15px; border-bottom: 2px solid @cTabBg;
    }
    CustomTabWidget QTabBar::tab:selected { background-color: @cTtopBg; color: @cTabText;
                                            border-bottom: 2px solid @cTtopBg;
    }
    CustomTabWidget QTabBar::tab:hover { background-color: @cTtopBg; color: @cTabText; }
    CustomTabWidget QToolBar { background: @cTtopBg; border-top: 2px solid @cTtopBg; }
    CustomTabWidget QToolButton {
      background-color: @cTabBg; border: 1px solid @cTabBg; border-radius: 3px;
      color: @cTabText; margin: 1px;
    }
    CustomTabWidget QToolButton:hover { background-color: @cTabSText; color: @cTabText; }
  )";
}

// ==========================================================
// [템플릿 조각 5] topToolBar 전용 템플릿
// ==========================================================
QString ThemeManager::getTopToolBarTemplate() const {
  return R"(
    /* Toolbar 자체 배경 (이전에 #a9a9a9 하드코딩 되어있던 부분) */
    topToolBar QToolBar {
        background-color: @cToolBg;
        border: none;
    }

    /* Toolbar 안의 텍스트 색상
    topToolBar QLabel {
        color: @cToolBg;
    }  */

    /* ComboBox 공통 스타일 */
    topToolBar QComboBox {
        background: @cToolText;
        color: @cToolTBg;
    }

    /* ComboBox 드롭다운 리스트 스타일 */
    topToolBar QComboBox QAbstractItemView {
        background-color: @cToolText;
        color: @cToolTBg;
        outline: none;
        selection-background-color: @CTcomBg; /* 선택된 항목 배경 (구버전 Qt용) */
    }

    topToolBar QComboBox QAbstractItemView::item:selected {
        background-color: @BtnBg;
        color: @CTcomText;
    }

    topToolBar QComboBox QAbstractItemView::item:hover {
        background-color: @upHover;
        color: @CTcomText;
    }
  )";
}

// ==========================================================
// [템플릿 조각 6] DocTabWidget 전용 템플릿
// ==========================================================
QString ThemeManager::getDocTabWidgetTemplate() const {
  return R"(
    /* 1. 바탕 영역 */
    DocTabWidget#DocTabWidget {
        background-color: @cDocBg;
        border: none;
    }

    /* 2. Home 버튼 */
    QPushButton#DocHomeButton {
        background: transparent;
        border: none;
        margin-left :3px;
        color: blue; /* 구글/윈도우 블루 톤 고정 */
        font-weight: bold;
    }
    QPushButton#DocHomeButton:hover {
        color: @cDocHover;
    }

    /* 3. 구분선 */
    QFrame#DocSeparator {
        background-color: @Border;
        border: none;
    }

    /* 4. 문서 탭바 (QTabBar) */
    QTabBar#DocTabBar::tab {
        border: 1px solid @cDocTB;
        background: @cDocTB;
        color: @cDocTg;
        padding: 2px 5px;
        border-bottom: none;
        margin-left: 1px;
        margin-bottom: 0px;
    }
    QTabBar#DocTabBar::tab:selected {
        background: @cDocTS;
        color: @cDocNewText;
    }
    QTabBar#DocTabBar::tab:hover:!selected {
        color: @cDocTBg;
        background: @cDocTg;
    }

    /* 5. 새 창(+) 버튼 */
    QPushButton#DocNewButton {
        padding: 3px 6px;
        background: @cDocNewBg;
        border-radius: 2px;
        font-weight: bold;
        font-size: 12px;
        color: @cDocNewText;
    }
    QPushButton#DocNewButton:hover {
        background: @cDocTg;
        padding: 5px 8px;
    }
  )";
}

// ==========================================================
// [템플릿 조각 7] DashboardWidget 전용 템플릿
// ==========================================================
QString ThemeManager::getDashboardTemplate() const {
  return R"(
    /* 스플리터 구분선 */
    QSplitter#DashSplitter::handle {
        background-color: @Border;
    }

    /* 왼쪽 사이드바 영역 */
    QWidget#DashSidebar {
        background-color: @Accent;
        border-top: 1px solid @Border;
        margin: 0px;
    }
    QLabel#DashSideLabel {
        font-weight: bold;
        font-size: 18px;
        margin: 10px;
        border: none;
        color: @TextBg;
    }
    QTreeView#DashFolderTree {
        border: 1px solid @Border;
        background-color: @WinBg;
        color: @TextBg;
    }

    /* 오른쪽 메인 콘텐츠 영역 */
    QWidget#DashContentArea {
        background-color: @Accent;
        border-top: 1px solid @Border;
        border-bottom: 1px solid @Border;
        margin: 0px;
    }
    QLabel#DashTitleLabel {
        font-weight: bold;
        font-size: 18px;
        border: none;
        margin-left: 5px;
        color: @TextBg;
    }

    /* 검색창 */
    QLineEdit#DashSearchBox {
        padding: 5px 10px;
        border: 1px solid @Border;
        border-radius: 4px;
        background-color: @WinBg;
        color: @TextBg;
    }
    QLineEdit#DashSearchBox:focus {
        border: 1px solid @Highlight; /* 포커스 시 강조색 */
    }

    /* 미리보기 변경 버튼 */
    QPushButton#DashToggleBtn {
        font-weight: bold;
        border: none;
        background: transparent;
        color: @dBtn;
    }
    QPushButton#DashToggleBtn:hover {
        color: @Highlight;
    }

    /* 스크롤 영역 및 내부 배경 */
    QScrollArea#DashScrollArea {
        border: none;
        background-color: @WinBg;
    }
    QWidget#DashScrollContent {
        background-color: @WinBg;
    }

    /* 섹션 타이틀 및 삭제 버튼 */
    QLabel#DashSectionTitle {
        font-weight: bold;
        font-size: 15px;
        color: @Text;
    }
    QPushButton#DashClearBtn {
        color: @Highlight;
        font-size: 15px;
        border: none;
        padding: 4px;
        background: transparent;
    }
    QPushButton#DashClearBtn:hover {
        text-decoration: underline;
    }
  )";
}

// ==========================================================
// [템플릿 조각 8] DrawSubWindow (QMdiSubWindow) 전용 템플릿
// ==========================================================
// [구조 설명]
// DrawSubWindow(QMdiSubWindow)
//   └─ DrawSubWindowContainer(setWidget, margin=0으로 꽉 채움)
//        └─ MyGraphicsView(layout으로 꽉 채움)
//             └─ viewport (씬 렌더링 → setBackgroundBrush로 제어)
//
// QMdiSubWindow / DrawSubWindowContainer 는 MyGraphicsView에 완전히 덮이므로
// CSS background-color / border 는 실제 픽셀에 표시되지 않음.
// → QMdiArea 배경(서브윈도우 바깥 영역)과 MyGraphicsView 프레임만 CSS로 제어.
QString ThemeManager::getSubWindowTemplate() const {
  return R"(
    /* MDI 영역 자체 배경 (서브윈도우 바깥 공간) */
    /* MyGraphicsView 프레임 제거 (기본값: Sunken Panel 테두리 있음) */
    QMdiSubWindow#DrawSubWindow QGraphicsView {
        border-top : 2px solid #808080;
    }
  )";
}
// ==========================================================
// [템플릿 조각 9] SubWindowControls 전용 템플릿 추가
// ==========================================================
QString ThemeManager::getSubWindowControlsTemplate() const {
  return R"(
    SubWindowControls QPushButton {
      background: transparent;
      border: none;
      border-radius: 2px;
      padding: 4px;
      color: @TextBg;
    }
    SubWindowControls QPushButton:hover {
      background-color: @Text;
      border-radius: 0px;
    }
    SubWindowControls QPushButton#SubWinCloseButton:hover {
      background-color: #e81123;
      color: white;
      border-radius: 0px;
    }
  )";
}
// ==========================================================
// [템플릿 조각 10] CommandDock 전용 템플릿 추가
// ==========================================================
QString ThemeManager::getCommandDockTemplate() const {
  return R"(
    /* 1. 최상위 컨테이너 */
    QWidget#CommandDockContainer {
        background-color: @cComBg;
        border-radius: 3px;
    }

    /* 2. 메인 콘텐츠 영역 */
    QWidget#CommandDockContent {
        background-color: @cConsBg;
    }

    /* 3. 히스토리 텍스트 영역 */
   QTextBrowser#CommandHistoryArea {
        background-color: @cHisBg;
        color: @cLatext;
        font-size: 14px;
        border: 1px solid @Border;
    }
    /* 4. 프롬프트 라벨 */
    QLabel#CommandPromptLabel {
        font-weight: bold;
        color: @cLatext;
        font-size: 13px;
    }

    /* 5. 입력창 */
    CommandInputLine#CommandInputLine {
        border: 1px solid @Border;
        padding: 1px;
        background: @cComBg;
        color: @cLatext;
        font-size: 14px;
        height: 25px;
    }

    /* 6. 토글 버튼 */
    QToolButton#CommandToggleBtn {
        border: none;
        background: transparent;
        color: @cLatext;
    }
    QToolButton#CommandToggleBtn:hover {
        background-color: @upHover;
        border-radius: 2px;
    }
  )";
}

// ==========================================================
// [템플릿 조각 11] LineTypeDock 전용 템플릿 추가
// ==========================================================
QString ThemeManager::getLineTypeDockTemplate() const {
  return R"(
    /* 1. 도크 위젯 전체 스타일 */
    LineTypeDock#LineTypeDock {
        border: 1px solid @Border;
    } 
    QWidget#LineTypeDockContent {
        background-color: @Accent; 
        border: none;
    } 
    /* 2. 도크 타이틀바 스타일 */
    LineTypeDock#LineTypeDock::title {
        background-color: @WinBg;
        color: @TextBg;
        padding: 6px;
        font-weight: bold;
        border: 1px solid @Accent;
    }
    /* 3. 테이블 뷰 스타일 */
    QTableView#LineTypeTableView {
        background-color: @WinBg;
        border: 1px solid @Border;
        gridline-color: @Border;
        outline: none;
    }

    QTableView#LineTypeTableView::item {
        padding: 4px;
        border-bottom: 1px solid @Border;
    }

    QTableView#LineTypeTableView::item:selected {
        background-color: @MenuBg;
        color: @AccentText;
        font-weight: bold;
    }

    QTableView#LineTypeTableView::item:hover:!selected {
        background-color: @upHover;
        color: @TextBg;
    }

    /* 2. 테이블 뷰 헤더 (QHeaderView) */
    QTableView#LineTypeTableView QHeaderView::section {
        background-color: @Accent;
        color: @TextBg;
        border: 1px solid @Border;
        padding: 4px;
        font-weight: bold;
    }

    /* 3. 상단 툴바 스타일 */
    QToolBar#LineTypeToolBar {
        background-color: transparent;
        border: none;
    }

    QToolBar#LineTypeToolBar QToolButton {
        background-color: transparent;
        border: 1px solid transparent;
        border-radius: 3px;
        padding: 3px;
    }

    QToolBar#LineTypeToolBar QToolButton:hover {
        background-color: @upHover;
        border: 1px solid @Border;
    }

    QToolBar#LineTypeToolBar QToolButton:pressed {
        background-color: @upHover;
    }
  )";
}

// ==========================================================
// [템플릿 조각 12] BlockPaletteDock 전용 템플릿 추가
// ==========================================================
QString ThemeManager::getBlockPaletteDockTemplate() const {
  return R"(
    QWidget#BlockPaletteDockContent {
        background-color: @Accent; 
    } 
    QWidget#BlockPaletteDock::title {
    background-color: @WinBg; /* 타이틀바 배경색 */
    color: @TextBg;            /* 타이틀바 글자색 */
    padding: 6px;              /* 내여백 */
    font-weight: bold;         /* 폰트 강조 */
    border-bottom: 1px solid @Border; /* 본문과의 구분선 */    
    } 
    /* 1. 블록 리스트 위젯 기본 배경 */
    QListWidget#BlockListWidget {
        border: none;
        background-color: @WinBg;
        outline: none;
    }

    /* 2. 개별 아이템 기본 상태 */
    QListWidget#BlockListWidget::item {
        border: 1px solid transparent;
        color: @TextBg;
    }

    /* 3. 아이템 선택 시 상태 */
    QListWidget#BlockListWidget::item:selected {
        background-color: @MenuBg;
        border: 1px solid @Border;
        color: @AccentText;
    }

    /* 4. 마우스 오버(Hover) 상태 */
    QListWidget#BlockListWidget::item:hover:!selected {
        background-color: @upHover;
        border: 1px solid @Border;
    }
  )";
}
// ==========================================================
// [템플릿 조각 13] LayerTableView 전용 템플릿 추가
// ==========================================================
QString ThemeManager::getLayerTableViewTemplate() const {
  return R"(
    QDockWidget#LayerTypeDock {
    border: 1px solid @Border;
    }
    QDockWidget#LayerTypeDock::title {
    background-color: @WinBg; /* 타이틀바 배경색 */
    color: @TextBg;            /* 타이틀바 글자색 */
    padding: 6px;              /* 내여백 */
    font-weight: bold;         /* 폰트 강조 */
    border-bottom: 1px solid @Border; /* 본문과의 구분선 */
    }
    /* 1. 테이블 뷰 스타일 */
    LayerTableView#LayerTypeTableView {
        background-color: @Accent; 
    }    
    LayerTableView#LayerTypeTableView QTableView {
        background-color: @WinBg; 
    }  
    QTableView#LayerTableView::item {
        padding: 4px;
        border : none;
        /*border-bottom: 1px solid @Border;*/
    }
    QTableView#LayerTableView::item:selected {
        background-color: @MenuBg;
        color: @AccentText;
        font-weight: bold;
    }
    QTableView#LayerTableView::item:hover:!selected {
        background-color: @upHover;
        color: @TextBg;
    }

    /* 2. 테이블 뷰 헤더 (QHeaderView) */
    QTableView#LayerTableView QHeaderView::section {
        background-color: @Accent;
        color: @TextBg;
        border: 1px solid @Border;
        padding: 4px;
        font-weight: bold;
    }

    /* 3. 상단 툴바 스타일 */
    QToolBar#LayerToolBar {
        background-color: transparent;
        border: none;
    }

    QToolBar#LayerToolBar QToolButton {
        background-color: transparent;
        border: 1px solid transparent;
        border-radius: 3px;
        padding: 3px;
    }

    QToolBar#LayerToolBar QToolButton:hover {
        background-color: @upHover;
        border: 1px solid @Border;
    }

    QToolBar#LayerToolBar QToolButton:pressed {
        background-color: @upHover;
    }
  )";
}
// ==========================================================
// [템플릿 조각 14] PropertyDock 전용 템플릿 추가
// ==========================================================
QString ThemeManager::getPropertyDockTemplate() const {
  return R"(
    /* 1. 입력창 통합 스타일 (LineEdit, ComboBox, SpinBox) */
    PropertyDock#PropertyDock QComboBox,
    PropertyDock#PropertyDock QLineEdit,
    PropertyDock#PropertyDock QDoubleSpinBox {
        background-color: @WinBg;
      /*  color: @TextBg;
        border: 1px solid @Border;
        border-radius: 2px;
        padding: 2px 5px;  */
    }

  PropertyDock#PropertyDock {
        background-color: @Accent; 
    }    
    PropertyDock#PropertyDock::title {
          background-color: @WinBg; /* 타이틀바 배경색 */
          color: @TextBg;            /* 타이틀바 글자색 */
          padding: 6px;              /* 내여백 */
          font-weight: bold;         /* 폰트 강조 */
          border-bottom: 1px solid @Border; /* 본문과의 구분선 */
    }    
    /* 1. 테이블 위젯 스타일 */

    PropertyDock#PropertyDock QTableWidget {
        background-color: @WinBg;      /* 속성창 바탕 */
        color: @TextBg;
        border: 1px solid @Border;
        gridline-color: @Border;
        outline: none;
        selection-background-color: @MenuBg;
        selection-color: @AccentText;
    }

    /* 2. 테이블 헤더 스타일 */
    
QTableWidget#PropertyTableWidgetTop QHeaderView::section {
    background-color: @hisBg;
    color: @TextBg;
    border: 1px solid @Border;
    padding: 4px;
    font-weight: bold;
}
/* 2. 하단/확장 테이블의 "병합된 행"은 QSS로 직접 제어하기 어려우므로, 
   테이블 전체의 기본 배경과 테두리만 설정합니다. */
QTableWidget#PropertyTableWidgetBottom QHeaderView::section,
QTableWidget#PropertyTableWidgetExt QHeaderView::section {
    background-color: @hisBg;
    border: 1px solid @Border;
    gridline-color: @Border;
}
    /* 3. 읽기 전용(비활성) 셀 스타일 (기존 코드의 회색 하드코딩 대체) */ 
    PropertyDock#PropertyDock QTableWidget::item:disabled {
        color: @Accent; /* 다크/라이트에 맞춰 적절한 흐린 색상 적용 */
        background-color: transparent;
    }
  )";
}

// ==========================================================
// [템플릿 조각 15] FileCard 전용 템플릿 추가
// ==========================================================
QString ThemeManager::getFileCardTemplate() const {
  return R"(
    QWidget#FileCard {
        background-color: @cCardBg;
        border: 1px solid @cCardBorder;
        border-radius: 8px;
    }
    QWidget#FileCard:hover {
        border: 1px solid @cCardHover;
    }
    QWidget#FileCard QLabel {
        color: @cCardTitle;
        background: transparent;
    }
    QWidget#FileCard QLabel#FileCardIcon {
        background-color: @cCardIconBg;
        border: 1px solid @cCardIconBorder;
        border-radius: 6px;
        padding: 1px;
    }
    QWidget#FileCard QLabel#FileNameLabel {
        background: transparent;
        font-weight: bold;
        font-size: 14px;
        color: @cCardTitle;
        margin: 0px;
    }
    QWidget#FileCard QLabel#FilePathLabel {
        background: transparent;
        font-size: 11px;
        color: @cCardDesc;
        margin: 0px;
    }
    QWidget#FileCard QFrame#FileCardTopLine {
        background-color: @cCardBorder;
        border: none;
        max-height: 1px;
        margin: 0px;
    }
  )";
}

// ==========================================================
// 메인 스타일 조립 및 색상 치환 함수
// ==========================================================
QString ThemeManager::getStyleSheet() const {
  // 1. 함수 호출하듯이 필요한 템플릿 조각들을 모두 하나로 이어 붙입니다.
  QString fullTemplate =
      getBaseWidgetTemplate() + getTabWidgetTemplate() + getMenuBarTemplate() +
      getTitleBarTemplate() + getTopToolBarTemplate() +
      getDocTabWidgetTemplate() + getDashboardTemplate() +
      getSubWindowTemplate() + getSubWindowControlsTemplate() +
      getCommandDockTemplate() + getLineTypeDockTemplate() +
      getBlockPaletteDockTemplate() + getLayerTableViewTemplate() +
      getPropertyDockTemplate() + getFileCardTemplate();

  // 2. 현재 테마에 맞는 색상 변수 설정
  QMap<QString, QString> colors;

  if (m_currentTheme == ThemeType::Dark) {
    colors["@cTitleBg"] = "#3b3b3b";
    colors["@cTitleText"] = "#ffffff";
    colors["@cTitleHover"] = "#949494";
    colors["@cMenuBg"] = "#414141";
    colors["@cMenuText"] = "#ffffff";
    colors["@cMenuSText"] = "#9e9e9e";
    colors["@cTabBg"] = "#4e4e4e";
    colors["@cTabText"] = "#ffffffff";
    colors["@cTabSText"] = "#888888";
    colors["@cTtopBg"] = "#222222";
    colors["@cToolBg"] = "#1b1b1b";
    colors["@cToolText"] = "#4e4e4e";
    colors["@cToolTBg"] = "#ffffff";
    colors["@CTcomBg"] = "#505050";
    colors["@CTcomText"] = "#ffffff";
    colors["@cDocBg"] = "#4e4e4e";
    colors["@cDocHover"] = "#ffffff";
    colors["@cDocTg"] = "#757575";
    colors["@cDocTS"] = "#2b2b2b";
    colors["@cDocTB"] = "#686868";
    colors["@cDocTBg"] = "#dfdfdf";
    colors["@cDocNewBg"] = "#666666";
    colors["@cDocNewText"] = "#ffffff";
    colors["@upHover"] = "#333333";
    colors["@cComBg"] = "#3b3b3b";
    colors["@cConsBg"] = "#202020";
    colors["@cHisBg"] = "#3b3b3b";
    colors["@cLatext"] = "#ffffec";
    colors["@cCardBg"] = "#363636";
    colors["@cCardBorder"] = "#555555";
    colors["@cCardHover"] = "#4285f4";
    colors["@cCardIconBg"] = "#2b2b2b";
    colors["@cCardIconBorder"] = "#444444";
    colors["@cCardTitle"] = "#ffffff";
    colors["@cCardDesc"] = "#bbbbbb";
    colors["@qMtabBg"] = "#464646";
    colors["@qMtabText"] = "#d3d3d3";
    colors["@qMtoolBg"] = "#272727";
    colors["@qMtoolText"] = "#ffffff";
    colors["@qMdiBg"] = "#000000";
    colors["@qStatuBg"] = "#505050";
    colors["@qStatuText"] = "#f1f1f1";
    colors["@dBtn"] = "#f0f0f0";
    colors["@Border"] = "#383838";
    colors["@BtnBg"] = "#90c8f0";
    colors["@hisBg"] = "#333333";
    colors["@WinBg"] = "#292929";
    colors["@Text"] = "#bbbbbb";
    colors["@TextBg"] = "#f0f0ef";
    colors["@Accent"] = "#272727";
    colors["@AccentText"] = "#f0f0f0";
    colors["@MenuBg"] = "#333333";
    colors["@Highlight"] = "#4285f4";

  } else if (m_currentTheme == ThemeType::CADClassic) {
    colors["@cTitleBg"] = "#696969";
    colors["@cTitleText"] = "#000000";
    colors["@cTitleHover"] = "#808080";
    colors["@cMenuBg"] = "#808080";
    colors["@cMenuText"] = "#000000";
    colors["@cMenuSText"] = "#d3d3d3";
    colors["@cTabBg"] = "#a9a9a9";
    colors["@cTabText"] = "#ffffff";
    colors["@cTabSText"] = "#d3d3d3";
    colors["@cTtopBg"] = "#696969";
    colors["@cToolBg"] = "#a9a9a9";
    colors["@cToolText"] = "#d3d3d3";
    colors["@cToolTBg"] = "#000000";
    colors["@CTcomBg"] = "#808080";
    colors["@CTcomText"] = "#303030ff";
    colors["@cDocBg"] = "#969696";
    colors["@cDocHover"] = "#ffffff";
    colors["@cDocTg"] = "#868686";
    colors["@cDocTS"] = "#535353";
    colors["@cDocTB"] = "#747474";
    colors["@cDocTBg"] = "#dfdfdf";
    colors["@cDocNewBg"] = "#666666";
    colors["@cDocNewText"] = "#ffffff";
    colors["@upHover"] = "#929292";
    colors["@cComBg"] = "#d3d3d3";
    colors["@cConsBg"] = "#a9a9a9";
    colors["@cHisBg"] = "#d3d3d3";
    colors["@cLatext"] = "#000000";
    colors["@cCardBg"] = "#c0c0c0";
    colors["@cCardBorder"] = "#808080";
    colors["@cCardHover"] = "#e0e0e0";
    colors["@cCardIconBg"] = "#f4f4f4";
    colors["@cCardIconBorder"] = "#dcdcdc";
    colors["@cCardTitle"] = "#333333";
    colors["@cCardDesc"] = "#777777";
    colors["@qMtabBg"] = "#a9a9a9";
    colors["@qMtabText"] = "#818181";
    colors["@qMtoolBg"] = "#808080";
    colors["@qMtoolText"] = "#ffffff";
    colors["@qMdiBg"] = "#696969";
    colors["@qStatuBg"] = "#808080";
    colors["@qStatuText"] = "#d3d3d3";
    colors["@hisBg"] = "#d3d3d3";
    colors["@dBtn"] = "white";
    colors["@BtnBg"] = "#90c8f0";
    colors["@Border"] = "#969696";
    colors["@WinBg"] = "#a9a9a9";
    colors["@Text"] = "#d3d3d3";
    colors["@TextBg"] = "black";
    colors["@Accent"] = "#808080";
    colors["@AccentText"] = "white";
    colors["@MenuBg"] = "#929292";
    colors["@Highlight"] = "#0052cc";
  } else {
      /* 밝은 색상 처리 -------*/
    colors["@cTitleBg"] = "#cfcfcf";
    colors["@cTitleText"] = "#000000";
    colors["@cTitleHover"] = "#aaaaaa";
    colors["@cMenuBg"] = "#dfdfdf";
    colors["@cMenuText"] = "#424242";
    colors["@cMenuSText"] = "#999999";
    colors["@cTabBg"] = "#d4d4d4";
    colors["@cTabText"] = "#000000";
    colors["@cTabSText"] = "#747474";
    colors["@cTtopBg"] = "#e7e7e7";
    colors["@cToolBg"] = "#eeeeee";
    colors["@cToolText"] = "#ebebeb";
    colors["@cToolTBg"] = "#000000";
    colors["@CTcomBg"] = "#808080";
    colors["@CTcomText"] = "#ffffff";
    colors["@cDocBg"] = "#d4d4d4";
    colors["@cDocHover"] = "#ffffffff";
    colors["@cDocTg"] = "#ffffffff";
    colors["@cDocTS"] = "#949494";
    colors["@cDocTB"] = "#b9b9b9";
    colors["@cDocTBg"] = "#d4d4d4";
    colors["@cDocNewBg"] = "#c9c9c9";
    colors["@cDocNewText"] = "#000000";
    colors["@upHover"] = "#ebebeb";
    colors["@cComBg"] = "#d3d3d3";
    colors["@cConsBg"] = "#a9a9a9";
    colors["@cHisBg"] = "#d3d3d3";
    colors["@cLatext"] = "#000000";
    colors["@cCardBg"] = "#f0f0f0";
    colors["@cCardBorder"] = "#dcdcdc";
    colors["@cCardHover"] = "#a0a0a0";
    colors["@cCardIconBg"] = "#ffffff";
    colors["@cCardIconBorder"] = "#e0e0e0";
    colors["@cCardTitle"] = "#000000";
    colors["@cCardDesc"] = "#666666";
    colors["@qMtabBg"] = "#a9a9a9";
    colors["@qMtabText"] = "#d3d3d3";
    colors["@qMtoolBg"] = "#a7a7a7";
    colors["@qMtoolText"] = "#070707";
    colors["@qMdiBg"] = "#696969";
    colors["@qStatuBg"] = "#808080";
    colors["@qStatuText"] = "#d3d3d3";
    colors["@hisBg"] = "#cecece";
    colors["@dBtn"] = "#000000";
    colors["@Border"] = "#979797";
    colors["@BtnBg"] = "#90c8f0";
    colors["@WinBg"] = "#ebebeb";
    colors["@Text"] = "#868686";
    colors["@TextBg"] = "black";
    colors["@Accent"] = "#a7a7a7";
    colors["@AccentText"] = "black";
    colors["@MenuBg"] = "#bebebe";
    colors["@Highlight"] = "#0052cc";
  }

  // 3. 조립된 거대한 템플릿 문자열에서 색상 태그를 일괄 치환
  // [중요] 길이가 긴 키부터 먼저 치환해야 부분 일치 문제(@Text vs @TextBg)를 방지할 수 있습니다.
  QStringList keys = colors.keys();
  std::sort(keys.begin(), keys.end(), [](const QString &a, const QString &b) {
      return a.length() > b.length();
  });

  // C++11 range-loop detach 경고 방지를 위해 인덱스 기반 루프 사용
  for (int i = 0; i < keys.size(); ++i) {
      const QString &key = keys.at(i);
      fullTemplate.replace(key, colors.value(key));
  }

  return fullTemplate;
}

void ThemeManager::applyTheme() {
  // [핵심] 모든 테마에서 Fusion 스타일 통일 사용
  // - Fusion: 순수 Qt 구현 → CSS(background-color 등)를 모든 위젯에 완전 적용
  // - Windows Native 스타일: GDI 위임 → CSS 일부 무시 (QMdiArea 등)
  qApp->setStyle(QStyleFactory::create("Fusion"));

  if (m_currentTheme == ThemeType::Dark) {
    // 다크 팔레트: 밝은 글자 + 어두운 배경
    QPalette darkPalette;
    darkPalette.setColor(QPalette::Window, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::WindowText, Qt::white);
    darkPalette.setColor(QPalette::Base, QColor(35, 35, 35));
    darkPalette.setColor(QPalette::AlternateBase, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::Text, Qt::white);
    darkPalette.setColor(QPalette::Button, QColor(45, 45, 45));
    darkPalette.setColor(QPalette::ButtonText, Qt::white);
    darkPalette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    darkPalette.setColor(QPalette::HighlightedText, Qt::black);
    darkPalette.setColor(QPalette::ToolTipBase, Qt::white);
    darkPalette.setColor(QPalette::ToolTipText, Qt::white);
    qApp->setPalette(darkPalette);
  } else if (m_currentTheme == ThemeType::CADClassic) {
    // 클래식 팔레트: 실버 배경 + 검정 글자 + 네이비 하이라이트
    QPalette classicPalette;
    classicPalette.setColor(QPalette::Window, QColor(211, 211, 211));
    classicPalette.setColor(QPalette::WindowText, Qt::black);
    classicPalette.setColor(QPalette::Base, Qt::white);
    classicPalette.setColor(QPalette::AlternateBase, QColor(0xd3, 0xd3, 0xd3));
    classicPalette.setColor(QPalette::Text, Qt::black);
    classicPalette.setColor(QPalette::Button, QColor(0xa9, 0xa9, 0xa9));
    classicPalette.setColor(QPalette::ButtonText, Qt::black);
    classicPalette.setColor(QPalette::Highlight, QColor(0x42, 0x85, 0xf4));
    classicPalette.setColor(QPalette::HighlightedText, Qt::white);
    qApp->setPalette(classicPalette);
  } else {
    // 라이트 팔레트: Fusion 기본 밝은 색
    QPalette lightPalette;
    lightPalette.setColor(QPalette::Window, QColor(211, 211, 211));
    lightPalette.setColor(QPalette::WindowText, Qt::black);
    lightPalette.setColor(QPalette::Base, Qt::white);
    lightPalette.setColor(QPalette::AlternateBase, QColor(0xd3, 0xd3, 0xd3));
    lightPalette.setColor(QPalette::Text, Qt::black);
    lightPalette.setColor(QPalette::Button, QColor(0xa9, 0xa9, 0xa9));
    lightPalette.setColor(QPalette::ButtonText, Qt::black);
    lightPalette.setColor(QPalette::Highlight, QColor(0x42, 0x85, 0xf4));
    lightPalette.setColor(QPalette::HighlightedText, Qt::white);
    qApp->setPalette(lightPalette);
  }

  qApp->setStyleSheet(getStyleSheet());
}