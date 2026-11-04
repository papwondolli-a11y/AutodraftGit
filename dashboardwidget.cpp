#include "dashboardwidget.h"
#include "filecard.h"
#include "filemanager.h"
#include "iappcontext.h"
#include "templatemanager.h"
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPixmap>
#include <QPushButton>
#include <QScrollArea>
#include <QSortFilterProxyModel>
#include <QSplitter>
#include <QTreeView>
#include <QVBoxLayout>

class CDBSortFilterProxyModel : public QSortFilterProxyModel {
public:
    CDBSortFilterProxyModel(QObject *parent = nullptr)
        : QSortFilterProxyModel(parent) {}

protected:
    bool filterAcceptsRow(int source_row,
                          const QModelIndex &source_parent) const override {
        QModelIndex index = sourceModel()->index(source_row, 0, source_parent);
        QFileSystemModel *model = qobject_cast<QFileSystemModel *>(sourceModel());
        if (!model) return true;
        if (model->isDir(index)) return true;
        QString fileName = model->fileName(index);
        return fileName.endsWith(".cdb", Qt::CaseInsensitive);
    }
};

DashboardWidget::DashboardWidget(IAppContext *ctx, QWidget *parent)
    : QWidget(parent), m_ctx(ctx) {

    // Dashboard 자체 위젯 배경색 적용을 위한 속성
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("DashboardWidget");

    setupUi();
    refreshRecentFiles();
    refreshTemplateCombo();
}

void DashboardWidget::setupUi() {
    QHBoxLayout *mainLayout = new QHBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    QSplitter *splitter = new QSplitter(Qt::Horizontal, this);
    splitter->setObjectName("DashSplitter");
    splitter->setHandleWidth(1);
    // QSS 제거됨

    // 데시보드 왼쪽: Folder Tree
    QWidget *sidebar = new QWidget(this);
    sidebar->setObjectName("DashSidebar");
    sidebar->setAttribute(Qt::WA_StyledBackground, true); // 배경색 렌더링 필수
    QVBoxLayout *sideLayout = new QVBoxLayout(sidebar);

    QLabel *sideLabel = new QLabel(tr("위치찾기"), this);
    sideLabel->setObjectName("DashSideLabel");

    m_folderTree = new QTreeView(this);
    m_folderTree->setObjectName("DashFolderTree");

    m_fileModel = new QFileSystemModel(this);
    m_fileModel->setRootPath(QDir::currentPath());
    m_fileModel->setFilter(QDir::AllDirs | QDir::NoDotAndDotDot);

    m_folderTree->setModel(m_fileModel);
    m_folderTree->setHeaderHidden(true);
    for (int i = 1; i < m_fileModel->columnCount(); ++i) {
        m_folderTree->setColumnHidden(i, true);
    }

    sideLayout->addWidget(sideLabel);
    // [수정] "위치찾기" 트리를 기존 크기의 2/3 로 줄이고, 남은 1/3 은 아래
    // "원형도면" 영역에 할당한다(수직 stretch 비율 2:1).
    sideLayout->addWidget(m_folderTree, 2);

    // [신규] "위치찾기" 아래: 원형도면 빠른 선택 영역 (제목 / 콤보박스 / 썸네일)
    QWidget *templateSection = new QWidget(this);
    templateSection->setObjectName("DashTemplateSection");
    QVBoxLayout *templateLayout = new QVBoxLayout(templateSection);
    templateLayout->setContentsMargins(0, 8, 0, 0);
    templateLayout->setSpacing(6);

    QLabel *templateTitle = new QLabel(tr("원형도면"), this);
    templateTitle->setObjectName("DashSideLabel");

    m_templateCombo = new QComboBox(this);
    m_templateCombo->setObjectName("DashTemplateCombo");

    m_templateThumbLabel = new QLabel(this);
    m_templateThumbLabel->setObjectName("DashTemplateThumb");
    m_templateThumbLabel->setAlignment(Qt::AlignCenter);
    m_templateThumbLabel->setMinimumHeight(100);
    m_templateThumbLabel->setFrameShape(QFrame::StyledPanel);

    templateLayout->addWidget(templateTitle);
    templateLayout->addWidget(m_templateCombo);
    templateLayout->addWidget(m_templateThumbLabel, 1);

    sideLayout->addWidget(templateSection, 1);

    // 데시보드 오른쪽 콘텐츠 영역
    QWidget *contentArea = new QWidget(this);
    contentArea->setObjectName("DashContentArea");
    contentArea->setAttribute(Qt::WA_StyledBackground, true); // 배경색 렌더링 필수
    QVBoxLayout *contentLayout = new QVBoxLayout(contentArea);
    contentLayout->setContentsMargins(0, 0, 0, 0);
    contentLayout->setSpacing(0);

    // Toolbar -- autodraftHome
    QWidget *toolbar = new QWidget(this);
    toolbar->setFixedHeight(60);
    QHBoxLayout *toolLayout = new QHBoxLayout(toolbar);

    QLabel *dashLabel = new QLabel(tr("AutoDraft v1.2(26.10.22) Home"), this);
    dashLabel->setObjectName("DashTitleLabel");

    // 도면 검색창
    m_searchBox = new QLineEdit(this);
    m_searchBox->setObjectName("DashSearchBox");
    m_searchBox->setPlaceholderText(tr("도면 파일 검색..."));
    m_searchBox->setFixedWidth(250);

    QPushButton *toggleBtn = new QPushButton(tr("미리보기 변경"), this);
    toggleBtn->setObjectName("DashToggleBtn");
    toggleBtn->setCheckable(true);
    toggleBtn->setChecked(true);

    toolLayout->addWidget(dashLabel);
    toolLayout->addStretch();
    toolLayout->addWidget(m_searchBox);
    toolLayout->addSpacing(10);
    toolLayout->addWidget(toggleBtn);

    // 스크롤 영역
    QScrollArea *scrollArea = new QScrollArea(this);
    scrollArea->setObjectName("DashScrollArea");
    scrollArea->setWidgetResizable(true);

    QWidget *scrollContent = new QWidget();
    scrollContent->setObjectName("DashScrollContent");
    scrollContent->setAttribute(Qt::WA_StyledBackground, true);
    QVBoxLayout *scrollLayout = new QVBoxLayout(scrollContent);
    scrollLayout->setContentsMargins(30, 20, 30, 20);
    scrollLayout->setSpacing(20);

    // Sections
    m_recentContainer = new QWidget(this);
    m_recentContainer->setLayout(new QGridLayout());
    m_recentContainer->layout()->setContentsMargins(0, 0, 0, 0);
    m_recentContainer->layout()->setSpacing(15);

    m_folderContainer = new QWidget(this);
    m_folderContainer->setLayout(new QGridLayout());
    m_folderContainer->layout()->setContentsMargins(0, 0, 0, 0);
    m_folderContainer->layout()->setSpacing(15);

    QHBoxLayout *recentHeader = new QHBoxLayout();
    QLabel *recentTitle = new QLabel(tr("최근 항목"), this);
    recentTitle->setObjectName("DashSectionTitle");

    QPushButton *clearBtn = new QPushButton(tr("전체 삭제"), this);
    clearBtn->setObjectName("DashClearBtn");
    clearBtn->setFlat(true);

    recentHeader->addWidget(recentTitle);
    recentHeader->addStretch();
    recentHeader->addWidget(clearBtn);

    QLabel *folderTitle = new QLabel(tr("폴더 전체 항목"), this);
    folderTitle->setObjectName("DashSectionTitle");

    scrollLayout->addLayout(recentHeader);
    scrollLayout->addWidget(m_recentContainer);
    scrollLayout->addSpacing(20);
    scrollLayout->addWidget(folderTitle);
    scrollLayout->addWidget(m_folderContainer);
    scrollLayout->addStretch();

    scrollArea->setWidget(scrollContent);

    contentLayout->addWidget(toolbar);
    contentLayout->addWidget(scrollArea);

    splitter->addWidget(sidebar);
    splitter->addWidget(contentArea);
    splitter->setStretchFactor(1, 4);

    mainLayout->addWidget(splitter);

    // 시그널 연결 (기존 유지)
    connect(m_folderTree->selectionModel(), &QItemSelectionModel::currentChanged,
            this, &DashboardWidget::onFolderSelected);
    connect(toggleBtn, &QPushButton::toggled, this,
            &DashboardWidget::toggleViewMode);
    connect(clearBtn, &QPushButton::clicked, this,
            &DashboardWidget::onClearRecentFiles);
    connect(m_searchBox, &QLineEdit::textChanged, this,
            &DashboardWidget::onSearchTextChanged);
    connect(m_templateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DashboardWidget::onTemplateComboChanged);
}

void DashboardWidget::onSearchTextChanged(const QString &text) {
  m_searchKeyword = text;

  // 리스트를 다시 그려서 검색어에 맞는 것만 표시합니다.
  refreshRecentFiles();
  if (!m_currentFolderPath.isEmpty()) {
    loadFolderFiles(m_currentFolderPath);
  }
}
void DashboardWidget::updateLayout() {
  refreshRecentFiles();
  // 폴더 리스트도 현재 선택된 폴더가 있다면 갱신 (생략 가능하면 패스)
}

int DashboardWidget::calculateColCount() {
  if (!m_previewMode)
    return 1;
  int w = width();
  if (w < 400)
    return 1;
  if (w < 700)
    return 2;
  if (w < 1000)
    return 3;
  return 4;
}

void DashboardWidget::resizeEvent(QResizeEvent *event) {
  static int lastCols = -1;
  int currentCols = calculateColCount();
  if (currentCols != lastCols) {
    lastCols = currentCols;
    refreshRecentFiles();
    // 현재 폴더 파일도 있다면 갱신
    // (이 부분은 폴더 경로 저장 필요하므로 간단히 하기 위해 최근 파일만 우선
    // 처리)
  }
  QWidget::resizeEvent(event);
}

void DashboardWidget::refreshRecentFiles() {
  if (!m_ctx || !m_ctx->fileManager() || !m_recentContainer)
    return;

  QLayoutItem *child;
  while ((child = m_recentContainer->layout()->takeAt(0)) != nullptr) {
    if (child->widget())
      child->widget()->deleteLater();
    delete child;
  }

  QStringList recentFiles = m_ctx->fileManager()->getRecentFiles();
  int colCount = calculateColCount();

  int displayIndex = 0; // 그리드 배치를 위한 별도 인덱스

  for (int i = 0; i < recentFiles.size(); ++i) {
    QString path = recentFiles[i];

    // [신규] 검색 필터링 로직
    if (!m_searchKeyword.isEmpty()) {
      QFileInfo fileInfo(path);
      // 파일명이나 경로에 검색어가 포함되지 않으면 건너뜀
      if (!fileInfo.fileName().contains(m_searchKeyword, Qt::CaseInsensitive)) {
        continue;
      }
    }

    QImage thumb = m_ctx->fileManager()->loadThumbnail(path);
    FileCard *card = new FileCard(
        path, thumb, m_previewMode ? FileCard::Preview : FileCard::List, this);

    connect(card, &FileCard::doubleClicked, this,
            &DashboardWidget::fileSelected);
    connect(card, &FileCard::requestRemove, this,
            &DashboardWidget::onRemoveRecentFile);

    // 표시할 아이템만 displayIndex를 증가시키며 그리드에 배치합니다.
    qobject_cast<QGridLayout *>(m_recentContainer->layout())
        ->addWidget(card, displayIndex / colCount, displayIndex % colCount);

    displayIndex++;
  }
}

void DashboardWidget::onClearRecentFiles() {
  if (m_ctx && m_ctx->fileManager()) {
    m_ctx->fileManager()->clearRecentFiles();
    refreshRecentFiles();
  }
}

void DashboardWidget::onRemoveRecentFile(const QString &filePath) {
  if (m_ctx && m_ctx->fileManager()) {
    m_ctx->fileManager()->removeRecentFile(filePath);
    refreshRecentFiles();
  }
}

void DashboardWidget::onFolderSelected(const QModelIndex &index) {
  QString path = m_fileModel->filePath(index);
  loadFolderFiles(path);
}

void DashboardWidget::loadFolderFiles(const QString &path) {
  if (!m_ctx || !m_ctx->fileManager())
    return;

  QLayoutItem *child;
  while ((child = m_folderContainer->layout()->takeAt(0)) != nullptr) {
    if (child->widget())
      child->widget()->deleteLater();
    delete child;
  }

  QDir dir(path);
  QStringList filters;
  filters << "*.cdb";
  QStringList files = dir.entryList(filters, QDir::Files);

  int colCount = calculateColCount();
  int displayIndex = 0;

  for (int i = 0; i < files.size(); ++i) {
    QString fullPath = dir.absoluteFilePath(files[i]);

    // [신규] 검색 필터링 로직
    if (!m_searchKeyword.isEmpty()) {
      if (!files[i].contains(m_searchKeyword, Qt::CaseInsensitive)) {
        continue;
      }
    }

    QImage thumb = m_ctx->fileManager()->loadThumbnail(fullPath);
    FileCard *card =
        new FileCard(fullPath, thumb,
                     m_previewMode ? FileCard::Preview : FileCard::List, this);

    connect(card, &FileCard::doubleClicked, this,
            &DashboardWidget::fileSelected);

    qobject_cast<QGridLayout *>(m_folderContainer->layout())
        ->addWidget(card, displayIndex / colCount, displayIndex % colCount);

    displayIndex++;
  }
}

void DashboardWidget::toggleViewMode(bool previewMode) {
  m_previewMode = previewMode;
  refreshRecentFiles();
  // Refresh folder files too (need to save current folder path)
  QModelIndex current = m_folderTree->currentIndex();
  if (current.isValid()) {
    onFolderSelected(current);
  }
}

// [신규] "원형도면" 콤보박스를 템플릿 목록으로 다시 채운다.
// 0번 항목은 항상 "빈 도면"이며, 현재 기본 원형도면이 있으면 그 항목이
// 콤보박스 상단(선택 표시)에 노출되도록 currentIndex 를 맞춘다.
void DashboardWidget::refreshTemplateCombo() {
  if (!m_templateCombo)
    return;

  m_templateCombo->blockSignals(true);
  m_templateCombo->clear();
  m_templateComboPaths.clear();

  m_templateCombo->addItem(tr("빈 도면"));
  m_templateComboPaths << QString();

  const QList<TemplateInfo> templates = TemplateManager::availableTemplates();
  for (int i = 0; i < templates.size(); ++i) {
    m_templateCombo->addItem(templates.at(i).name);
    m_templateComboPaths << templates.at(i).path;
  }

  const QString defPath = TemplateManager::defaultTemplatePath();
  int idx = 0;
  if (!defPath.isEmpty()) {
    for (int i = 1; i < m_templateComboPaths.size(); ++i) {
      if (QFileInfo(m_templateComboPaths.at(i)) == QFileInfo(defPath)) {
        idx = i;
        break;
      }
    }
  }
  m_templateCombo->setCurrentIndex(idx);
  m_templateCombo->blockSignals(false);

  updateTemplateThumbnail(idx);
}

// [신규] 콤보박스에서 현재 노출된 항목의 썸네일을 표시한다.
void DashboardWidget::updateTemplateThumbnail(int index) {
  if (!m_templateThumbLabel)
    return;

  if (index < 0 || index >= m_templateComboPaths.size() || !m_ctx ||
      !m_ctx->fileManager()) {
    m_templateThumbLabel->clear();
    return;
  }

  const QString path = m_templateComboPaths.at(index);
  const QImage thumb =
      path.isEmpty() ? QImage() : m_ctx->fileManager()->loadThumbnail(path);
  if (thumb.isNull()) {
    m_templateThumbLabel->clear();
    return;
  }

  const int w = m_templateThumbLabel->width() > 0
                    ? m_templateThumbLabel->width()
                    : 160;
  m_templateThumbLabel->setPixmap(
      QPixmap::fromImage(thumb).scaled(w, 120, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
}

// [신규] 콤보박스에서 고른 항목을 기본 원형도면(Ctrl+N)으로 사용한다.
void DashboardWidget::onTemplateComboChanged(int index) {
  updateTemplateThumbnail(index);

  if (index < 0 || index >= m_templateComboPaths.size())
    return;

  TemplateManager::setDefaultTemplatePath(m_templateComboPaths.at(index));
}
