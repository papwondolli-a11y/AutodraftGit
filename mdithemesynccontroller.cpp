#include "mdithemesynccontroller.h"
#include <QTimer>

MdiThemeSyncController::MdiThemeSyncController(QMdiArea *mdiArea, ThemeManager *themeManager, QObject *parent)
    : QObject(parent), m_mdiArea(mdiArea) {

    if (themeManager) {
        // 람다 없이 포인터 함수로 시그널-슬롯 연결
        connect(themeManager, &ThemeManager::themeAboutToChange,
                this, &MdiThemeSyncController::onThemeAboutToChange);

        connect(themeManager, &ThemeManager::themeChanged,
                this, &MdiThemeSyncController::onThemeChanged);
    }
}

void MdiThemeSyncController::onThemeAboutToChange() {
    m_savedStates.clear();
    if (!m_mdiArea) return;

    QList<QMdiSubWindow *> subWindows = m_mdiArea->subWindowList();

    // C++11 detach 경고 방지를 위한 인덱스 기반 루프
    for (int i = 0; i < subWindows.size(); ++i) {
        QMdiSubWindow *sub = subWindows.at(i);

        // 초기 생성 및 최초 최대화 완료 전인 서브윈도우는 테마 상태 백업 대상에서 제외
        if (sub->property("isInitializing").toBool()) {
            continue;
        }

        WindowState state;

        // 테마가 바뀌어 크기가 망가지기 직전의 진짜 상태를 백업
        state.geometry = sub->geometry();
        state.isMaximized = sub->isMaximized();
        state.isMinimized = sub->isMinimized();

        m_savedStates.insert(sub, state);
    }
}

void MdiThemeSyncController::onThemeChanged(ThemeManager::ThemeType type) {
    Q_UNUSED(type);

    // UI 스레드가 스타일을 모두 렌더링한 직후(50ms 후) 복원 실행
    QTimer::singleShot(50, this, &MdiThemeSyncController::restoreGeometries);
}

void MdiThemeSyncController::restoreGeometries() {
    if (!m_mdiArea) return;

    QList<QMdiSubWindow *> subWindows = m_mdiArea->subWindowList();

    // 백업해둔 상태를 꺼내어 원래 상태에 맞게 복원 (인덱스 기반 루프)
    for (int i = 0; i < subWindows.size(); ++i) {
        QMdiSubWindow *sub = subWindows.at(i);

        if (m_savedStates.contains(sub)) {
            WindowState state = m_savedStates.value(sub);

            if (state.isMinimized) {
                sub->showMinimized();
            } else if (state.isMaximized) {
                sub->showMaximized();
            } else {
                // 일반 상태이거나 "Normal 상태로 꽉 차게 만든 창"일 경우
                // 저장해둔 Geometry 값 그대로 복원
                sub->showNormal();
                sub->setGeometry(state.geometry);
            }
        }
    }

    // 복원이 끝났으므로 백업본 초기화
    m_savedStates.clear();
}