#ifndef THEMEMANAGER_H
#define THEMEMANAGER_H

#include <QColor>
#include <QObject>
#include <QString>

class ThemeManager : public QObject {
  Q_OBJECT
public:
  enum class ThemeType { Light, Dark, CADClassic };

  explicit ThemeManager(QObject *parent = nullptr);

  ThemeType currentTheme() const { return m_currentTheme; }
  void setTheme(ThemeType type);

  QColor canvasBackground() const;
  QColor windowBackground() const; // UI 배경색 (@WinBg 와 동일)
  QColor gridColor() const;
  QColor ghostColor() const;

  QString getStyleSheet() const;

signals:
  void themeAboutToChange(); // [추가] 테마가 바뀌기 직전에 발생할 시그널
  void themeChanged(ThemeManager::ThemeType type);

private:
  ThemeType m_currentTheme;
  void applyTheme();

  // 각 위젯별 QSS 템플릿을 제공하는 함수들
  QString getTabWidgetTemplate() const;
  QString getMenuBarTemplate() const;
  QString getBaseWidgetTemplate() const;
  QString getTitleBarTemplate() const;
  QString getTopToolBarTemplate() const;
  QString getDocTabWidgetTemplate() const;
  QString getDashboardTemplate() const;
  QString getSubWindowTemplate() const;
  QString getSubWindowControlsTemplate() const;
  QString getCommandDockTemplate() const;
  QString getLineTypeDockTemplate() const;
  QString getBlockPaletteDockTemplate() const;
  QString getLayerTableViewTemplate() const;
  QString getPropertyDockTemplate() const;
  QString getFileCardTemplate() const;
};

#endif // THEMEMANAGER_H