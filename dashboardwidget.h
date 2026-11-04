#ifndef DASHBOARDWIDGET_H
#define DASHBOARDWIDGET_H

#include <QFileSystemModel>
#include <QStringList>
#include <QWidget>

class IAppContext;
class QTreeView;
class QFlowLayout; // Custom layout for cards
class QVBoxLayout;
class QLineEdit;
class QComboBox;
class QLabel;

class DashboardWidget : public QWidget {
  Q_OBJECT
public:
  explicit DashboardWidget(IAppContext *ctx, QWidget *parent = nullptr);

  void refreshRecentFiles();
  void refreshTemplateCombo(); // [신규] "원형도면" 콤보박스/썸네일 새로고침

signals:
  void fileSelected(const QString &filePath);

private slots:
  void onFolderSelected(const QModelIndex &index);
  void toggleViewMode(bool previewMode);
  void updateLayout();
  void onClearRecentFiles();
  void onRemoveRecentFile(const QString &filePath);
  void onSearchTextChanged(const QString &text); // [추가] 검색어 변경 처리 슬롯
  void onTemplateComboChanged(int index); // [신규] 원형도면 콤보 선택 변경

private:
  void setupUi();
  void loadFolderFiles(const QString &path);
  int calculateColCount();
  void updateTemplateThumbnail(int index); // [신규]

protected:
  void resizeEvent(QResizeEvent *event) override;

  IAppContext *m_ctx;
  QTreeView *m_folderTree;
  QFileSystemModel *m_fileModel;

  QWidget *m_recentContainer;
  QWidget *m_folderContainer;
  QLineEdit *m_searchBox; // [추가] 검색창 위젯
  bool m_previewMode = true;
  QString m_searchKeyword;     // [추가] 현재 입력된 검색어 저장
  QString m_currentFolderPath; // [추가] 현재 선택된 폴더 경로 보관용

  // [신규] "위치찾기" 아래 원형도면(템플릿) 빠른 선택 영역
  QComboBox *m_templateCombo;
  QLabel *m_templateThumbLabel;
  QStringList m_templateComboPaths; // 콤보 인덱스 ↔ 템플릿 경로 (0 = "빈 도면")
};

#endif // DASHBOARDWIDGET_H
