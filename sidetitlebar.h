#ifndef SIDETITLEBAR_H
#define SIDETITLEBAR_H

#include <QWidget>
#include <QDockWidget>
#include <QMouseEvent>
#include <QToolButton>
#include <QBoxLayout> // QVBoxLayout, QHBoxLayout의 부모

class SideTitleBar : public QWidget
{
    Q_OBJECT
public:
    // [수정] 생성자에서 방향(가로/세로)을 설정받음 (기본값: 세로)
    explicit SideTitleBar(Qt::Orientation orient, QWidget *parent = nullptr);

    void setTargetDock(QDockWidget *dock);

signals:
    void closeClicked();
    void floatClicked();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    QDockWidget *targetDock;
    bool isPressed = false;
    QPoint dragStartPos;
    Qt::Orientation orientation; // 방향 저장 변수

    QToolButton *btnClose;
    QToolButton *btnFloat;

    void setupUi();
};

#endif // SIDETITLEBAR_H
