#ifndef CUSTOMMENUBAR_H
#define CUSTOMMENUBAR_H

#include <QMenuBar>
class CommandInfo;

class CustomMenuBar : public QMenuBar {
    Q_OBJECT
public:
    explicit CustomMenuBar(QWidget *parent = nullptr);
    QAction *addCommandAction(const CommandInfo &info);

    void setCommandLineChecked(bool checked);
    void setCommandFilterChecked(bool checked);

signals:
    void toggleCommandLine(bool visible);
    void toggleLineWeight(bool visible);
    void toggleCommandFilter(bool visible);
    void toggleLineTypeDock(bool visible);
    void toggleLayerTableDock(bool visible);
    void toggleBlockPaletteDock(bool visible);
    void togglePropertyDock(bool visible);
    void themeSelected(int themeType);

    void newFileRequested();
    void openFileRequested();
    void saveFileRequested();
    void saveAsFileRequested();
    void recentFileSelected(const QString &filePath);

public slots:
    void updateRecentFileActions();
    void addRecentFile(const QString &filePath);
    void clearRecentFiles();

    void setLineTypeDockChecked(bool checked);
    void setLayerTableDockChecked(bool checked);
    void setBlockPaletteDockChecked(bool checked);
    void setPropertyDockChecked(bool checked);

    // 현재 활성화된 테마에 체크 표시 (외부에서 테마 변경 시 호출)
    void setCheckedTheme(int themeType);

private slots:
    void onOpenRecentFile();

    // [수정] 람다 함수를 대체하기 위한 테마 선택 슬롯 추가
    void onThemeLight();
    void onThemeDark();
    void onThemeCADClassic();

    // [i18n] 언어 전환 (한국어 / English) — 재시작으로 반영
    void onLanguageKorean();
    void onLanguageEnglish();

    void onOpenUserManual();

private:
    QMenu *makeMenu(const QString &mainName);
    void applyLanguage(const QString &lang);
    QHash<QString, QMenu *> m_menuMap;

    QAction *m_actCommandLine = nullptr;
    QAction *m_actCommandFilter = nullptr;
    QAction *m_actDynamicTooltip = nullptr;
    QAction *m_actLineWeight = nullptr;

    QAction *m_actLineTypeDock     = nullptr;
    QAction *m_actLayerTableDock   = nullptr;
    QAction *m_actBlockPaletteDock = nullptr;
    QAction *m_actPropertyDock     = nullptr;

    // 테마 액션 (체크가능 라디오 그룹)
    QAction *m_actThemeCAD   = nullptr;
    QAction *m_actThemeLight = nullptr;
    QAction *m_actThemeDark  = nullptr;

    // 언어 액션 (체크가능 라디오 그룹)
    QAction *m_actLangKo = nullptr;
    QAction *m_actLangEn = nullptr;

    QMenu *m_recentMenu = nullptr;

    static const int MaxRecentFiles = 10;
    QAction *m_recentFileActs[MaxRecentFiles];
    QAction *m_clearRecentAct = nullptr;
};

#endif // CUSTOMMENUBAR_H