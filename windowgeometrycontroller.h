#ifndef WINDOWGEOMETRYCONTROLLER_H
#define WINDOWGEOMETRYCONTROLLER_H

#include <QObject>
#include <QRect>
#include <QWidget>

class WindowGeometryController : public QObject {
    Q_OBJECT
public:
    explicit WindowGeometryController(QWidget *targetWindow, QObject *parent = nullptr);

    void setCustomNormalSize(int width, int height);
    // [신규] 노트북처럼 화면이 작은 환경에서 1024x768 같은 고정 크기가 화면보다
    // 크거나 비율이 안 맞는 문제를 피하려고, 현재 화면(작업 영역 기준) 크기의
    // ratio(예: 2.0/3.0)만큼을 복원 크기로 계산해 적용한다.
    void setCustomNormalSizeByScreenRatio(double ratio);

public slots:
    void toggleMaximizeRestore();

signals:
    // [신규] toggleMaximizeRestore()가 실제로 어떤 동작을 했는지 그대로 알린다.
    // Qt의 isMaximized()/changeEvent(WindowStateChange)는 도면을 열어 QOpenGLWidget
    // 뷰포트를 쓰는 MDI 서브윈도우가 함께 최대화돼 있는 상황에서, 첫 토글 사이클에
    // 한해 우리가 방금 지시한 동작과 다른(한 박자 늦은) 값을 보고하는 경우가
    // 반복 확인됐다. Qt에 상태를 다시 물어보는 대신, 우리가 마지막으로 지시한
    // 동작을 그대로 신뢰해 버튼 아이콘을 그 자리에서 확정적으로 갱신한다.
    void maximizedStateChanged(bool isMaximized);

private:
    QWidget *m_targetWindow;
    // [버그 수정] Qt/Windows의 내부 "복원 크기" 기억이 FramelessWindowHint 창에서는
    // (특히 파일을 열어 MDI 서브윈도우가 함께 최대화돼 있는 등 레이아웃이 무거워지면)
    // 깨지는 경우가 있어(showNormal()이 거의 최대화 크기 그대로 남아 크기 변화가
    // 안 보임), 여기서 직접 마지막 "일반(Normal)" 크기를 기억해뒀다가 복원 때마다
    // 강제로 재적용한다.
    QRect m_normalGeometry;
    // [신규] 토글 판단의 유일한 기준. Qt의 isMaximized()를 매번 다시 묻지 않고,
    // 우리가 마지막으로 지시한 동작을 그대로 기억한다. 앱은 항상 최대화 상태로
    // 시작하므로 true로 초기화한다(main.cpp에서 showMaximized() 예약).
    bool m_isMaximized = true;
};

#endif // WINDOWGEOMETRYCONTROLLER_H
