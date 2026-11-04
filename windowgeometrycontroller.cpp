#include "windowgeometrycontroller.h"
#include <QApplication>
#include <QScreen>
#include <QRect>
#include <QTimer>
#include <QPointer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

WindowGeometryController::WindowGeometryController(QWidget *targetWindow, QObject *parent)
    : QObject(parent), m_targetWindow(targetWindow) {
}

void WindowGeometryController::setCustomNormalSize(int width, int height) {
    if (!m_targetWindow) return;

    QScreen *screen = QApplication::primaryScreen();
    if (!screen) return;

    QRect screenGeometry = screen->availableGeometry();
    int x = screenGeometry.center().x() - (width / 2);
    int y = screenGeometry.center().y() - (height / 2);

    // [핵심] 창이 띄워지기 전, 또는 일반(Normal) 상태일 때만 크기를 세팅합니다.
    // Qt 자체의 복원 크기 기억(normalGeometry)에도 의존하지만, 파일을 열어 MDI
    // 서브윈도우가 함께 최대화되는 등 레이아웃이 무거워지면 이게 깨지는 경우가
    // 있어 우리가 직접 m_normalGeometry로도 기억해둔다.
    if (!m_targetWindow->isMaximized()) {
        m_targetWindow->setGeometry(x, y, width, height);
        m_normalGeometry = QRect(x, y, width, height);
    }
}

void WindowGeometryController::setCustomNormalSizeByScreenRatio(double ratio) {
    if (!m_targetWindow) return;

    QScreen *screen = QApplication::primaryScreen();
    if (!screen) return;

    // [버그 수정] 1024x768 같은 고정 픽셀 크기는 노트북처럼 작은 화면에서는
    // 화면보다 크거나 화면 비율과 안 맞아 이상하게 보일 수 있다. 현재 화면의
    // 작업 영역(작업표시줄 제외) 크기에 비례한 크기를 계산해 적용한다.
    QRect screenGeometry = screen->availableGeometry();
    int width = qRound(screenGeometry.width() * ratio);
    int height = qRound(screenGeometry.height() * ratio);
    setCustomNormalSize(width, height);
}

void WindowGeometryController::toggleMaximizeRestore() {
    if (!m_targetWindow) return;

    // [설계] Qt의 isMaximized()를 매번 다시 묻지 않고 우리가 마지막으로 지시한
    // 동작(m_isMaximized)만 판단 기준으로 쓴다 — 도면을 열어 QOpenGLWidget
    // 뷰포트를 쓰는 MDI 서브윈도우가 함께 최대화돼 있을 때는, 첫 토글 사이클에
    // 한해 Qt가 실제 상태를 우리가 방금 지시한 것과 다르게(한 박자 늦게) 보고하는
    // 경우가 있었다. 우리가 직접 시킨 동작이니 우리가 제일 정확히 안다.
    if (m_isMaximized) {
        // 강제로 resize()를 호출하지 않고, 오직 Qt의 기본 상태 전환 함수만 사용합니다.
        // 이렇게 해야 Windows OS와의 충돌(경고 메시지)이 발생하지 않습니다.
        m_targetWindow->showNormal();

        // [버그 수정] Qt가 기억하는 내부 복원 크기가 깨져 showNormal()이 크기를
        // 거의 바꾸지 않는(최대화 크기 그대로 남는) 경우가 있다. 우리가 직접
        // 기억해둔 정상 크기를 덮어써서 항상 확실하게 복원되도록 한다.
        if (m_normalGeometry.isValid()) {
            QRect target = m_normalGeometry;
            QPointer<QWidget> win(m_targetWindow);
            // [버그 수정] showNormal() 직후 같은 호출 스택에서 곧바로 지오메트리를
            // 적용하면(특히 도면을 열어 QOpenGLWidget 뷰포트를 쓰는 MDI 서브윈도우가
            // 함께 최대화돼 있을 때) Qt/Windows가 아직 상태 전환을 끝내기 전이라
            // 요청이 무시된다. 다음 이벤트 루프 턴으로 미뤄 적용한다.
            QTimer::singleShot(0, this, [win, target]() {
                if (!win) return;
#ifdef Q_OS_WIN
                // Qt의 setGeometry()는 내부적으로 "여전히 최대화 상태"로 오판하면
                // 요청을 조용히 거부한다(창 크기가 거의 안 바뀜). HWND에
                // SetWindowPos()로 직접 배치해 그 내부 가드를 우회한다.
                HWND hwnd = reinterpret_cast<HWND>(win->winId());
                if (hwnd) {
                    if (::IsZoomed(hwnd))
                        ::ShowWindow(hwnd, SW_RESTORE);

                    QScreen *screen = win->screen();
                    qreal dpr = screen ? screen->devicePixelRatio() : 1.0;
                    int x = qRound(target.x() * dpr);
                    int y = qRound(target.y() * dpr);
                    int w = qRound(target.width() * dpr);
                    int h = qRound(target.height() * dpr);
                    ::SetWindowPos(hwnd, nullptr, x, y, w, h,
                                   SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
                    // [버그 수정] SetWindowPos()는 Win32 API를 직접 불러 픽셀
                    // 크기만 바꿀 뿐, Qt 자신의 내부 windowState 캐시는 갱신하지
                    // 않는다. 이 캐시가 여전히 "최대화"로 남아있으면(대시보드에는
                    // 없고 도면을 연 뒤에만 재현되는 QOpenGLWidget/MDI 조합의
                    // 문제) 상태바의 크기조절 그립(우측 하단 점 3개)이 계속
                    // 숨겨져 있고, 테두리 드래그로도 리사이즈가 전혀 먹히지
                    // 않는다 — 둘 다 Qt가 "이 창은 최대화라 리사이즈 불가"로
                    // 스스로 판단하기 때문이다. Qt의 공식 API로 한 번 더 상태를
                    // 명시해 내부 캐시를 확실히 Normal로 되돌린다.
                    win->setWindowState(win->windowState() & ~Qt::WindowMaximized);
                    return;
                }
#endif
                win->setGeometry(target);
            });
        }

        m_isMaximized = false;
    } else {
        // 최대화하기 직전의 "현재(=일반) 크기"를 최신 상태로 기억해둔다
        // (사용자가 직접 창 크기를 조절해뒀을 수도 있으므로).
        m_normalGeometry = m_targetWindow->geometry();
        m_targetWindow->showMaximized();

#ifdef Q_OS_WIN
        // [버그 수정] 복원(축소) 방향과 마찬가지로, 도면을 열어 QOpenGLWidget
        // 뷰포트의 MDI 서브윈도우가 함께 최대화돼 있을 때는 이 showMaximized()가
        // Qt 내부 상태만 바꾸고 네이티브 화면 전환은 조용히 무시되는 경우가 있다
        // (버튼을 눌러도 아이콘만 바뀌고 화면은 그대로). 한 틱 뒤에 실제로
        // 최대화됐는지 확인해, 안 됐으면 Win32 API로 직접 걸어준다.
        QPointer<QWidget> win(m_targetWindow);
        QTimer::singleShot(0, this, [win]() {
            if (!win) return;
            HWND hwnd = reinterpret_cast<HWND>(win->winId());
            if (hwnd && !::IsZoomed(hwnd)) {
                ::ShowWindow(hwnd, SW_MAXIMIZE);
                // Qt 자신의 내부 windowState 캐시도 명시적으로 맞춰준다(복원
                // 방향의 동일한 수정 참고).
                win->setWindowState(win->windowState() | Qt::WindowMaximized);
            }
        });
#endif

        m_isMaximized = true;
    }

    // [신규] 타이틀바 버튼 아이콘은 changeEvent(WindowStateChange) 쪽 isMaximized()
    // 재조회를 거치지 않고 여기서 바로, 우리가 확정한 상태로 갱신한다.
    emit maximizedStateChanged(m_isMaximized);
}
