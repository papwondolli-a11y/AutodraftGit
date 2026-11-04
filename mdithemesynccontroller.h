#ifndef MDITHEMESYNCCONTROLLER_H
#define MDITHEMESYNCCONTROLLER_H

#include <QObject>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QMap>
#include <QRect>
#include "thememanager.h"

class MdiThemeSyncController : public QObject {
    Q_OBJECT
public:
    explicit MdiThemeSyncController(QMdiArea *mdiArea, ThemeManager *themeManager, QObject *parent = nullptr);

private slots:
    void onThemeAboutToChange();
    void onThemeChanged(ThemeManager::ThemeType type);
    void restoreGeometries();

private:
    QMdiArea *m_mdiArea;

    // 윈도우의 상태를 기억하기 위한 구조체
    struct WindowState {
        QRect geometry;
        bool isMaximized;
        bool isMinimized;
    };

    // 서브윈도우 포인터를 키로 사용하여 이전 상태 저장
    QMap<QMdiSubWindow*, WindowState> m_savedStates;
};

#endif // MDITHEMESYNCCONTROLLER_H