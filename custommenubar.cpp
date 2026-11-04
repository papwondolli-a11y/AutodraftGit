#include "custommenubar.h"
#include "CommandInfo.h"
#include "helpdialog.h"
#include "thememanager.h"
#include <QActionGroup>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QMessageBox>
#include <QProcess>
#include <QSettings>
#include <algorithm>
#include <qmenu.h>

CustomMenuBar::CustomMenuBar(QWidget *parent) : QMenuBar(parent) {
  setCursor(Qt::ArrowCursor);
  // [수정] 기존에 하드코딩 되어 있던 setStyleSheet(...) 완전히 제거
  // 이제 ThemeManager에서 스타일을 제어합니다.

  QMenu *fileMenu = makeMenu(tr("파일"));
  m_recentMenu = fileMenu->addMenu(tr("최근 항목"));

  // 인덱스 기반 루프 사용 (C++11 range-loop detach 경고 방지)
  for (int i = 0; i < MaxRecentFiles; ++i) {
    m_recentFileActs[i] = new QAction(this);
    m_recentFileActs[i]->setVisible(false);
    connect(m_recentFileActs[i], &QAction::triggered, this,
            &CustomMenuBar::onOpenRecentFile);
    m_recentMenu->addAction(m_recentFileActs[i]);
  }
  m_recentMenu->addSeparator();
  m_clearRecentAct = m_recentMenu->addAction(tr("항목 지우기"), this,
                                             &CustomMenuBar::clearRecentFiles);

  updateRecentFileActions();
  fileMenu->addSeparator();

  makeMenu(tr("옵션"));
  QMenu *viewMenu = makeMenu(tr("보기"));
  QMenu *settingsMenu = makeMenu(tr("설정"));
  QMenu *helpMenu = makeMenu(tr("도움말"));

  helpMenu->addAction(tr("사용자 매뉴얼"), this, &CustomMenuBar::onOpenUserManual);

  // [i18n] 설정 > 언어 / Language  (한국어 / English, 재시작으로 반영)
  QMenu *langMenu = settingsMenu->addMenu(tr("언어 / Language"));
  QActionGroup *langGroup = new QActionGroup(this);
  langGroup->setExclusive(true);

  m_actLangKo = langMenu->addAction(QStringLiteral("한국어"));
  m_actLangEn = langMenu->addAction(QStringLiteral("English"));
  m_actLangKo->setCheckable(true);
  m_actLangEn->setCheckable(true);
  langGroup->addAction(m_actLangKo);
  langGroup->addAction(m_actLangEn);

  const QString curLang =
      QSettings().value(QStringLiteral("ui/language"), QStringLiteral("en")).toString();
  m_actLangKo->setChecked(curLang == QStringLiteral("ko"));
  m_actLangEn->setChecked(curLang != QStringLiteral("ko"));

  connect(m_actLangKo, &QAction::triggered, this, &CustomMenuBar::onLanguageKorean);
  connect(m_actLangEn, &QAction::triggered, this, &CustomMenuBar::onLanguageEnglish);

  m_actCommandLine = new QAction(tr("명령어 라인 보기"), this);
  m_actCommandLine->setCheckable(true);
  m_actCommandLine->setChecked(true);
  viewMenu->addAction(m_actCommandLine);

  m_actCommandFilter = new QAction(tr("명령어 필터 보기"), this);
  m_actCommandFilter->setCheckable(true);
  m_actCommandFilter->setChecked(true);
  viewMenu->addAction(m_actCommandFilter);

  m_actLineWeight = new QAction(tr("선 두께 보기"), this);
  m_actLineWeight->setCheckable(true);
  m_actLineWeight->setChecked(false);
  viewMenu->addAction(m_actLineWeight);

  connect(m_actCommandLine, &QAction::toggled, this,
          &CustomMenuBar::toggleCommandLine);
  connect(m_actLineWeight, &QAction::toggled, this,
          &CustomMenuBar::toggleLineWeight);
  connect(m_actCommandFilter, &QAction::toggled, this,
          &CustomMenuBar::toggleCommandFilter);

  viewMenu->addSeparator();

  m_actLineTypeDock = new QAction(tr("선 종류 도크"), this);
  m_actLineTypeDock->setCheckable(true);
  m_actLineTypeDock->setChecked(true);
  viewMenu->addAction(m_actLineTypeDock);

  m_actLayerTableDock = new QAction(tr("레이어 테이블"), this);
  m_actLayerTableDock->setCheckable(true);
  m_actLayerTableDock->setChecked(true);
  viewMenu->addAction(m_actLayerTableDock);

  m_actBlockPaletteDock = new QAction(tr("블록 팔레트"), this);
  m_actBlockPaletteDock->setCheckable(true);
  m_actBlockPaletteDock->setChecked(true);
  viewMenu->addAction(m_actBlockPaletteDock);

  m_actPropertyDock = new QAction(tr("속성 도크"), this);
  m_actPropertyDock->setCheckable(true);
  m_actPropertyDock->setChecked(true);
  viewMenu->addAction(m_actPropertyDock);

  connect(m_actLineTypeDock, &QAction::toggled, this, &CustomMenuBar::toggleLineTypeDock);
  connect(m_actLayerTableDock, &QAction::toggled, this, &CustomMenuBar::toggleLayerTableDock);
  connect(m_actBlockPaletteDock, &QAction::toggled, this, &CustomMenuBar::toggleBlockPaletteDock);
  connect(m_actPropertyDock, &QAction::toggled, this, &CustomMenuBar::togglePropertyDock);

  QMenu *themeMenu = makeMenu(tr("테마"));

  // 테마 액션 생성 시 checkable + exclusive 그룹으로 구성
  QActionGroup *themeGroup = new QActionGroup(this);
  themeGroup->setExclusive(true);

  m_actThemeCAD   = themeMenu->addAction(tr("Draft 기본색상"));
  m_actThemeLight = themeMenu->addAction(tr("밝은 색상"));
  m_actThemeDark  = themeMenu->addAction(tr("어두운 색상"));

  m_actThemeCAD->setCheckable(true);
  m_actThemeLight->setCheckable(true);
  m_actThemeDark->setCheckable(true);

  themeGroup->addAction(m_actThemeCAD);
  themeGroup->addAction(m_actThemeLight);
  themeGroup->addAction(m_actThemeDark);

  // 기본 선택: CADClassic
  m_actThemeCAD->setChecked(true);

  connect(m_actThemeLight, &QAction::triggered, this, &CustomMenuBar::onThemeLight);
  connect(m_actThemeDark,  &QAction::triggered, this, &CustomMenuBar::onThemeDark);
  connect(m_actThemeCAD,   &QAction::triggered, this, &CustomMenuBar::onThemeCADClassic);
}

// [수정] 람다를 대체하는 슬롯 구현부
void CustomMenuBar::onThemeLight() {
  emit themeSelected(static_cast<int>(ThemeManager::ThemeType::Light));
  setCheckedTheme(static_cast<int>(ThemeManager::ThemeType::Light));
}
void CustomMenuBar::onThemeDark() {
  emit themeSelected(static_cast<int>(ThemeManager::ThemeType::Dark));
  setCheckedTheme(static_cast<int>(ThemeManager::ThemeType::Dark));
}
void CustomMenuBar::onThemeCADClassic() {
  emit themeSelected(static_cast<int>(ThemeManager::ThemeType::CADClassic));
  setCheckedTheme(static_cast<int>(ThemeManager::ThemeType::CADClassic));
}

// [i18n] 언어 전환 ------------------------------------------------------------
// 런타임 재번역(retranslateUi) 경로가 없으므로, 선택을 저장한 뒤 앱을 재시작해
// main.cpp 의 QTranslator 로드 분기를 다시 태운다.
void CustomMenuBar::onLanguageKorean()  { applyLanguage(QStringLiteral("ko")); }
void CustomMenuBar::onLanguageEnglish() { applyLanguage(QStringLiteral("en")); }

void CustomMenuBar::onOpenUserManual() {
  auto *dlg = new HelpDialog(this);
  dlg->setAttribute(Qt::WA_DeleteOnClose);
  dlg->show();
  dlg->raise();
  dlg->activateWindow();
}

void CustomMenuBar::applyLanguage(const QString &lang) {
  QSettings settings;
  const QString cur =
      settings.value(QStringLiteral("ui/language"), QStringLiteral("en")).toString();
  if (cur == lang)
    return;

  const bool ko = (lang == QStringLiteral("ko"));
  const QString title = ko ? QStringLiteral("언어 변경") : tr("Change Language");
  const QString body =
      ko ? QStringLiteral("언어를 한국어로 바꾸려면 프로그램을 다시 시작해야 합니다.\n지금 재시작할까요?")
         : tr("The application must restart to switch the language to English.\nRestart now?");

  const QMessageBox::StandardButton ans = QMessageBox::question(
      this, title, body, QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);

  if (ans != QMessageBox::Yes) {
    // 취소 시 체크 상태를 원래대로 되돌린다.
    if (m_actLangKo) m_actLangKo->setChecked(cur == QStringLiteral("ko"));
    if (m_actLangEn) m_actLangEn->setChecked(cur != QStringLiteral("ko"));
    return;
  }

  settings.setValue(QStringLiteral("ui/language"), lang);
  settings.sync();

  QProcess::startDetached(QApplication::applicationFilePath(),
                          QApplication::arguments().mid(1));
  QApplication::quit();
}

void CustomMenuBar::setCheckedTheme(int themeType) {
  if (!m_actThemeCAD || !m_actThemeLight || !m_actThemeDark) return;

  // blockSignals로 triggered 재발방지
  m_actThemeCAD->blockSignals(true);
  m_actThemeLight->blockSignals(true);
  m_actThemeDark->blockSignals(true);

  m_actThemeCAD->setChecked(themeType == static_cast<int>(ThemeManager::ThemeType::CADClassic));
  m_actThemeLight->setChecked(themeType == static_cast<int>(ThemeManager::ThemeType::Light));
  m_actThemeDark->setChecked(themeType == static_cast<int>(ThemeManager::ThemeType::Dark));

  m_actThemeCAD->blockSignals(false);
  m_actThemeLight->blockSignals(false);
  m_actThemeDark->blockSignals(false);
}

QMenu *CustomMenuBar::makeMenu(const QString &mainName) {
  QHash<QString, QMenu *>::const_iterator it = m_menuMap.constFind(mainName);
  if (it != m_menuMap.constEnd())
    return it.value();

  QMenu *menu = addMenu(mainName);
  m_menuMap.insert(mainName, menu);
  return menu;
}

QAction *CustomMenuBar::addCommandAction(const CommandInfo &info) {
  QMenu *menu = makeMenu(info.mainName());

  QAction *act = new QAction(menu);
  act->setText(info.subName());

  if (!info.iconName().isEmpty())
    act->setIcon(QIcon(info.iconName()));

  if (!info.shortcut().isEmpty())
    act->setShortcut(QKeySequence(info.shortcut()));

  menu->addAction(act);
  return act;
}

void CustomMenuBar::setCommandLineChecked(bool checked) {
  if (m_actCommandLine) {
    m_actCommandLine->blockSignals(true);
    m_actCommandLine->setChecked(checked);
    m_actCommandLine->blockSignals(false);
  }
}

void CustomMenuBar::setCommandFilterChecked(bool checked) {
  if (m_actCommandFilter) {
    m_actCommandFilter->blockSignals(true);
    m_actCommandFilter->setChecked(checked);
    m_actCommandFilter->blockSignals(false);
  }
}

void CustomMenuBar::setLineTypeDockChecked(bool checked) {
  if (m_actLineTypeDock) {
    m_actLineTypeDock->blockSignals(true);
    m_actLineTypeDock->setChecked(checked);
    m_actLineTypeDock->blockSignals(false);
  }
}

void CustomMenuBar::setLayerTableDockChecked(bool checked) {
  if (m_actLayerTableDock) {
    m_actLayerTableDock->blockSignals(true);
    m_actLayerTableDock->setChecked(checked);
    m_actLayerTableDock->blockSignals(false);
  }
}

void CustomMenuBar::setBlockPaletteDockChecked(bool checked) {
  if (m_actBlockPaletteDock) {
    m_actBlockPaletteDock->blockSignals(true);
    m_actBlockPaletteDock->setChecked(checked);
    m_actBlockPaletteDock->blockSignals(false);
  }
}

void CustomMenuBar::setPropertyDockChecked(bool checked) {
  if (m_actPropertyDock) {
    m_actPropertyDock->blockSignals(true);
    m_actPropertyDock->setChecked(checked);
    m_actPropertyDock->blockSignals(false);
  }
}

void CustomMenuBar::updateRecentFileActions() {
  QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
  QStringList files = settings.value("recentFileList").toStringList();

  int numRecentFiles = std::min((int)files.size(), (int)MaxRecentFiles);

  for (int i = 0; i < numRecentFiles; ++i) {
    QString text = tr("&%1 %2").arg(i + 1).arg(QFileInfo(files[i]).fileName());
    m_recentFileActs[i]->setText(text);
    m_recentFileActs[i]->setData(files[i]);
    m_recentFileActs[i]->setVisible(true);
  }
  for (int j = numRecentFiles; j < MaxRecentFiles; ++j) {
    m_recentFileActs[j]->setVisible(false);
  }

  m_recentMenu->setEnabled(numRecentFiles > 0);
}

void CustomMenuBar::addRecentFile(const QString &filePath) {
  if (filePath.isEmpty())
    return;

  QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
  QStringList files = settings.value("recentFileList").toStringList();
  files.removeAll(filePath);
  files.prepend(filePath);
  while (files.size() > MaxRecentFiles)
    files.removeLast();

  settings.setValue("recentFileList", files);
  updateRecentFileActions();
}

void CustomMenuBar::clearRecentFiles() {
  QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
  settings.setValue("recentFileList", QStringList());
  updateRecentFileActions();
}

void CustomMenuBar::onOpenRecentFile() {
  QAction *action = qobject_cast<QAction *>(sender());
  if (action)
    emit recentFileSelected(action->data().toString());
}