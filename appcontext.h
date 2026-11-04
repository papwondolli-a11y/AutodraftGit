#ifndef APPCONTEXT_H
#define APPCONTEXT_H

#include "iappcontext.h"
#include "hatchpatternmanager.h"
#include <QBrush>
#include <QObject>
#include <QPen>
#include <QPointer> // QPointer 사용을 위해 추가
#include <QUndoStack>
#include <memory>

class MainWindow;
class QMdiArea;
class CustomStatusBar;
class DrawSubWindow;
class MyGraphicsScene;
class MyGraphicsView;
class AbstractTool;
class SelectTool;
class SnapManager;
class CommandDock;
class InputParser; // InputParser 전방 선언 추가
class LineTypeManager;
class StyleManager;
class LayerManager;
class SelectionManager;
class EntityFactory;
class ThemeManager;
class ResourceManager;
class FileManager;
class SubWindowFloatController;

class AppContext : public QObject, public IAppContext {
  Q_OBJECT
public:
  explicit AppContext(QMdiArea *mdiArea, CustomStatusBar *statusBar,
                      CommandDock *commandDock, QObject *parent = nullptr);
  ~AppContext();

  // [IAppContext 구현] CommandDock 반환
  CommandDock *commandDock() const override;
  QMdiArea *mdiArea() const override { return m_mdiArea; }

  // [신규] 서브윈도우 플로팅 컨트롤러 등록 및 접근
  void setFloatController(SubWindowFloatController *fc) { m_floatController = fc; }
  SubWindowFloatController *floatController() const { return m_floatController; }

  // [신규] 활성 윈도우를 강제로 설정하는 함수 (DrawSubWindow가 호출)
  void setLastActiveWindow(DrawSubWindow *w);
  // [신규] 파서 접근 함수
  // [Phase 3] 반환 전에 활성 도면의 길이 단위로 InputParser 의 환산 배율을
  //   갱신한다 → 모든 parseCoordinate 호출부가 자동으로 올바른 단위를 쓴다.
  class InputParser *parser() const override;
  class LineTypeManager *lineTypeManager() const override {
    return m_lineTypeManager.get();
  }
  class StyleManager *styleManager() const override {
    return m_styleManager.get();
  }
  class LayerManager *layerManager() const override;
  class ThemeManager *themeManager() const override;
  class ResourceManager *resourceManager() const override;
  class QUndoStack *undoStack() const override;
  class FileManager *fileManager() const override {
    return m_fileManager.get();
  }

  class HatchPatternManager *hatchPatternManager() const override {
      return m_hatchPatternManager.get();
  }
  // =========================================================
  // 1. 문서 및 뷰 접근
  // =========================================================
  DrawSubWindow *activeDrawWindow() const override;
  void addSubWindow(DrawSubWindow *window) override;
  MyGraphicsScene *activeScene() const override;
  MyGraphicsView *activeView() const override;

  // =========================================================
  // 2. 도구(Tool) 제어
  // =========================================================
  void startTool(AbstractTool *tool) override;
  void finishCurrentTool() override;

  // 기본 도구(SelectTool) 접근 (필요 시)
  SelectTool *selectTool() const { return m_selectTool.get(); }

  // =========================================================
  // 3. 스냅(Snap) 관리
  // =========================================================
  SnapManager *snapManager() const override { return m_snapManager.get(); }
  SelectionManager *selectionManager() const override;
  EntityFactory *entityFactory() const override {
    return m_entityFactory.get();
  }

  // =========================================================
  // 4. 전역 속성 (Pen, Brush)
  // =========================================================
  QPen currentPen() const override;
  QBrush currentBrush() const override;

  // =========================================================
  // 5. UI 피드백
  // =========================================================
  void showStatusText(const QString &text) override;

  // 특정 레이어의 가시성(On/Off)을 모든 씬에 업데이트하는 함수
  void updateLayerVisibility(const QString &layerName, bool isVisible);

  // [신규] 폰트 속성 제어 함수
  void setCurrentFontFamily(const QString &family);
  QString currentFontFamily() const override;

  void setCurrentFontSize(qreal size);
  qreal currentFontSize() const override;

  // 글자 서식(굵게/기울임/밑줄). 바뀌면 currentFontChanged 도 함께 발행해
  // 입력 중인 글자 도구가 즉시 반영하도록 한다.
  void setCurrentFontStyle(bool bold, bool italic, bool underline);
  bool currentFontBold() const override { return m_currentFontBold; }
  bool currentFontItalic() const override { return m_currentFontItalic; }
  bool currentFontUnderline() const override { return m_currentFontUnderline; }

  void setTextLayerName(const QString &layerName) override;
  QString textLayerName() const override;

  // [신규] 최근 파일 목록 추가
  void addToRecentFiles(const QString &filePath) override;

  // [IAppContext 구현] 블록 팔레트 표시
  void showBlockPalette() override;

  // 블록 라이브러리 폴더 활성화
  void activateBlockLibrary(const QString &subFolder) override;

  // [신규] 메뉴/커맨드 활성화 제어
  void setCommandEnabled(const QString &id, bool enabled) override;

  // [신규] 대시보드 "원형도면" 콤보 실시간 갱신 요청
  void refreshDashboardTemplates() override;

  // [신규] 도면창 간 객체 복사/붙여넣기용 클립보드
  void setClipboardItems(const QList<class CadItem *> &items) override;
  QList<class CadItem *> clipboardItems() const override;


signals:
  // MainWindow에게 서브 윈도우 추가를 요청하는 시그널
  void requestAddSubWindow(class DrawSubWindow *window);

  // [신규] 상태 변경 알림 시그널 (Observer Pattern)
  void currentLayerChanged(const QString &layerName);
  void currentFontChanged(const QString &family, qreal size);
  void currentFontStyleChanged(bool bold, bool italic, bool underline);
  // 선택된 글자들의 서식(모두 켜짐일 때만 true) — 툴바 B/I/U 버튼 표시 동기화용.
  // 현재 글자 서식(새로 쓸 글자)은 바꾸지 않는다.
  void selectedTextStyleChanged(bool bold, bool italic, bool underline);
  // 선택된 글자들의 글꼴/크기(도면 축척 적용 전 값). 글자마다 다르면 family 는 빈 문자열,
  // size 는 0 이하. 툴바 글꼴 콤보 표시 동기화용이며 현재 글꼴(새 글자)은 바꾸지 않는다.
  void selectedTextFontChanged(const QString &family, qreal size);
  void textLayerChanged(const QString &layerName);
  void currentPenChanged(const QPen &pen);
  void currentBrushChanged(const QBrush &brush);
  void layerPropertyChanged(const QString &layerName);
  void recentFileAdded(const QString &filePath);
  void requestShowBlockPalette(); // [신규] 블록 팔레트 표시 요청
  void requestActivateBlockLibrary(const QString &path);
  void requestSetCommandEnabled(const QString &id, bool enabled); // [신규]
  void requestRefreshDashboardTemplates(); // [신규]


public slots:
  void toggleLineWeight(bool show); // [신규] 모든 씬의 선가중치 표시 모드 토글

private slots:
  void onLayerPropertyChanged(const QString &layerName);
  void onApplicationFocusChanged(QWidget *old, QWidget *now); // [신규] 전역 포커스 변경 감지

private:
  QMdiArea *m_mdiArea;
  CustomStatusBar *m_statusBar;
  CommandDock *m_commandDock;

  // 핵심 매니저 및 도구 (스마트 포인터로 관리)
  std::unique_ptr<SnapManager> m_snapManager;
  std::unique_ptr<SelectionManager>
      m_selectionManager; // [신규] SelectionManager 멤버
  std::unique_ptr<EntityFactory> m_entityFactory;
  std::unique_ptr<SelectTool> m_selectTool;
  std::unique_ptr<InputParser> m_inputParser;

  // 기본 스타일 속성
  QPen m_defaultPen;
  QBrush m_defaultBrush;
  QPointer<DrawSubWindow>
      m_lastActiveWindow; // [수정] DrawSubWindow* -> QPointer로 변경
  std::unique_ptr<LineTypeManager> m_lineTypeManager;
  std::unique_ptr<StyleManager> m_styleManager; // 스마트 포인터 사용 권장
  std::unique_ptr<LayerManager> m_layerManager;
  std::unique_ptr<ThemeManager> m_themeManager;
  std::unique_ptr<ResourceManager> m_resourceManager;
  std::unique_ptr<QUndoStack> m_undoStack;
  std::unique_ptr<FileManager> m_fileManager;
  std::unique_ptr<HatchPatternManager> m_hatchPatternManager;
  // [신규] 현재 선택된 폰트 이름과 크기를 보관하는 변수
  QString m_currentFontFamily;
  qreal m_currentFontSize;
  bool m_currentFontBold = false;
  bool m_currentFontItalic = false;
  bool m_currentFontUnderline = false;
  QString m_textLayerName;
  SubWindowFloatController *m_floatController = nullptr;
  QList<class CadItem *> m_clipboardItems; // [신규] 도면창 간 복사/붙여넣기 클립보드
};

#endif // APPCONTEXT_H
