#ifndef CUSTOMTITLEBAR_H
#define CUSTOMTITLEBAR_H

#include <QWidget>
#include <QAction>

class QToolBar;
class QToolButton;
class QPushButton;

class CustomTitleBar : public QWidget {
    Q_OBJECT
public:
    explicit CustomTitleBar(QWidget *parent = nullptr);

    // MainWindow에서 단축키 등록 및 상태 동기화를 위해 액션/툴바 포인터 제공
    QAction* undoAction() const { return m_undoAction; }
    QAction* redoAction() const { return m_redoAction; }
    QToolBar* toolBar() const { return m_titleToolBar; }

    // 최대화 상태에 따라 아이콘을 변경하는 함수
    void updateMaximizeButtonIcon(bool isMaximized);

signals:
    // MainWindow로 전달할 이벤트 시그널
    void saveRequested();
    void saveAsRequested();
    void printRequested();
    void minimizeRequested();
    void maximizeRequested();
    void closeRequested();

private slots:
    // 람다를 대체할 내부 슬롯 함수들
    void onSaveClicked();
    void onSaveAsClicked();
    void onPrintClicked();
    void onMinimizeClicked();
    void onMaximizeClicked();
    void onCloseClicked();

private:
    QToolBar *m_titleToolBar = nullptr;
    QAction *m_undoAction = nullptr;
    QAction *m_redoAction = nullptr;
    QPushButton *m_maximizeButton;
};

#endif // CUSTOMTITLEBAR_H