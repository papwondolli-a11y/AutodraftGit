#ifndef DRAWSUBWINDOW_H
#define DRAWSUBWINDOW_H

#include <QList>
#include <QMdiSubWindow>
#include "thememanager.h" // [추가] ThemeManager 연동을 위해 필요

class MyGraphicsScene;
class MyGraphicsView;
class AbstractTool;
class AppContext;
class LayerManager;
class QUndoStack;
class SelectionManager;
class CadItem;

class DrawSubWindow : public QMdiSubWindow {
    Q_OBJECT
public:
    explicit DrawSubWindow(QWidget *parent = nullptr);
    ~DrawSubWindow();

    MyGraphicsScene *scene() const;
    MyGraphicsView *view() const;
    AbstractTool *tool() const;
    AbstractTool *defaultTool() const { return m_defaultTool; }
    void setTool(AbstractTool *tool);
    LayerManager *layerManager() const;
    QUndoStack *undoStack() const;
    SelectionManager *selectionManager() const;

    void setContext(AppContext *ctx);
    void setDefaultTool(AbstractTool *tool);
    void resetToDefaultTool();
    void useDefaultTool() { resetToDefaultTool(); }

    // [신규] 새 도면 창을 최초로 열 때, MDI 서브윈도우가 최대화되어 실제
    // 화면 크기가 확정된 뒤 초기 화면(A3 기준, 원점 좌측 하단 배치)을
    // 맞추기 위해 지연 호출을 예약한다. NewSubWindowCommand에서
    // addSubWindow() 직후 호출한다.
    void scheduleInitialViewFit();

signals:
    void coordinateChanged(QPointF pos);

private slots:
    void onToolFinished();
    void onSelectionChanged();
    void flushSelectionChanged(); // 몰린 selectionChanged를 한 번만 처리
    void onLayerPropertyChanged(const QString &name);
    void onThemeChanged(ThemeManager::ThemeType type); // [신규] 람다 대체용 테마 슬롯
    void onInitialViewFitTimeout(); // [신규] scheduleInitialViewFit()의 지연 타이머 콜백

protected:
    void focusInEvent(QFocusEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override; // [신규] 마우스 클릭 시 활성 창 갱신
    void changeEvent(QEvent *event) override; // [신규] 활성화 상태 변경 감지
    void closeEvent(QCloseEvent *event) override;

private:
    MyGraphicsScene *myScene;
    MyGraphicsView *myView;
    AbstractTool *m_activeTool;
    AbstractTool *m_defaultTool;
    AppContext *m_ctx;
    LayerManager *m_layerManager;
    QUndoStack *m_undoStack;
    SelectionManager *m_selectionManager;
    bool m_themeConnected = false; // setContext() 이중 호출 시 이중 connect 방지
    bool m_selectionUpdatePending = false; // flushSelectionChanged 예약 여부
};

#endif // DRAWSUBWINDOW_H