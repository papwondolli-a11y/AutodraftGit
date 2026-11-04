#ifndef IAPPCONTEXT_H
#define IAPPCONTEXT_H

#include <QBrush>
#include <QList>
#include <QPen>
#include <QString>

// [전방 선언] 헤더 의존성을 줄이기 위해 클래스 이름만 알려줌
class MyGraphicsScene;
class MyGraphicsView;
class ToolBase;
class DrawSubWindow;
class AbstractTool;
class CommandDock;
class QMdiArea;
class FileManager;
class HatchPatternManager;
class CadItem;

class IAppContext {
public:
  virtual ~IAppContext() {}

  // =========================================================
  // 1. 문서 및 뷰 접근 (Document Access)
  // =========================================================

  // [IAppContext 구현] CommandDock 반환
  virtual CommandDock *commandDock() const = 0;

  // 현재 활성 도면 창 반환
  virtual DrawSubWindow *activeDrawWindow() const = 0;

  // 현재 활성화된 도면(Scene)을 반환
  virtual MyGraphicsScene *activeScene() const = 0;

  // 현재 활성화된 뷰(View)를 반환
  virtual MyGraphicsView *activeView() const = 0;

  // =========================================================
  // 2. 도구 제어 (Tool Management)
  // =========================================================

  // 새로운 도구를 시작 (기존 도구는 자동 종료됨)
  // 예: ctx->startTool(new LineTool(...));
  virtual void startTool(AbstractTool *tool) = 0;

  // // 현재 도구를 종료하고 기본 상태(SelectTool)로 복귀
  virtual void finishCurrentTool() = 0;

  // =========================================================
  // 3. 전역 속성 (Global Attributes)
  // =========================================================

  // 현재 설정된 펜(선 색상, 두께) 반환
  virtual QPen currentPen() const = 0;

  // 현재 설정된 브러시(채우기 색상) 반환
  virtual QBrush currentBrush() const = 0;

  // =========================================================
  // 4. 사용자 피드백 (User Feedback)
  // =========================================================

  // 상태바에 메시지 표시
  virtual void showStatusText(const QString &text) = 0;
  virtual void addSubWindow(DrawSubWindow *window) = 0;
  virtual QMdiArea *mdiArea() const = 0;
  virtual class SnapManager *snapManager() const = 0;
  virtual class SelectionManager *selectionManager() const = 0;
  virtual class EntityFactory *entityFactory() const = 0;
  virtual class InputParser *parser() const = 0;
  virtual class LineTypeManager *lineTypeManager() const = 0;
  virtual class StyleManager *styleManager() const = 0;
  virtual class ThemeManager *themeManager() const = 0;
  virtual class ResourceManager *resourceManager() const = 0;
  virtual class LayerManager *layerManager() const = 0;
  virtual class QUndoStack *undoStack() const = 0;
  virtual class FileManager *fileManager() const = 0;

  virtual class HatchPatternManager *hatchPatternManager() const = 0; // 해치 패턴
  // [신규] 폰트 속성 제어
  virtual QString currentFontFamily() const = 0;
  virtual qreal currentFontSize() const = 0;
  // 글자 서식(굵게/기울임/밑줄) — 기본은 꺼짐
  virtual bool currentFontBold() const { return false; }
  virtual bool currentFontItalic() const { return false; }
  virtual bool currentFontUnderline() const { return false; }
  virtual QString textLayerName() const = 0;
  virtual void setTextLayerName(const QString &layerName) = 0;

  // [신규] 최근 파일 목록 관리
  virtual void addToRecentFiles(const QString &filePath) = 0;

  // [신규] 블록 팔레트 도크 표시
  virtual void showBlockPalette() = 0;

  // 블록 라이브러리 폴더 활성화 (subFolder: "furniture", "sink" 등)
  virtual void activateBlockLibrary(const QString &subFolder) = 0;

  // [신규] 메뉴/커맨드 활성화 제어. id 는 CommandInfo::id() (예: "menu.file.newfromtemplate").
  virtual void setCommandEnabled(const QString &id, bool enabled) = 0;

  // [신규] 기본 원형도면(템플릿)이 바뀌었을 때 대시보드의 "원형도면" 콤보/썸네일을
  // 즉시 갱신하도록 요청한다 ("템플릿 보기 > 교체" 등에서 호출).
  virtual void refreshDashboardTemplates() = 0;

  // [신규] 도면창(서브윈도우) 간 객체 복사/붙여넣기용 앱 전역 클립보드.
  // clone()된(원본 씬과 무관한) CadItem들을 보관하며, 소유권을 넘겨받는다.
  virtual void setClipboardItems(const QList<CadItem *> &items) = 0;
  virtual QList<CadItem *> clipboardItems() const = 0;

};

#endif // IAPPCONTEXT_H
