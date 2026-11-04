#include "batchprintdialog.h"

#include "iappcontext.h"
#include "drawsubwindow.h"
#include "mygraphicsscene.h"
#include "filemanager.h"
#include "acadrustimporter.h"
#include "dxfmanager.h"

#include <QApplication>
#include <QDir>
#include <QSettings>
#include <QStyle>
#include <QMdiArea>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QMessageBox>
#include <QFrame>

BatchPrintDialog::BatchPrintDialog(IAppContext *ctx, QWidget *parent)
    : QDialog(parent), m_ctx(ctx), m_radioCdb(nullptr), m_radioDwg(nullptr),
      m_radioDxf(nullptr), m_folderCombo(nullptr), m_browseButton(nullptr),
      m_clearHistoryButton(nullptr), m_sourceListWidget(nullptr),
      m_previewLabel(nullptr), m_btnAddToList(nullptr),
      m_selectedListWidget(nullptr), m_btnRemove(nullptr), m_btnUp(nullptr),
      m_btnDown(nullptr), m_btnOk(nullptr), m_btnCancel(nullptr) {
    setWindowTitle(tr("일괄 출력"));
    setupUI();
    resize(760, 480);
    loadFolderHistory();
}

void BatchPrintDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    mainLayout->addWidget(new QLabel(
        tr("왼쪽 : 미리보기 확인 후 \"추가\"로 출력 목록 구성(순서=출력순서)  /  오른쪽 : 폴더에서 도면 찾아보기"),
        this));

    QHBoxLayout *centerLayout = new QHBoxLayout();

    // ==================== 왼쪽: 미리보기 + 출력 목록 ====================
    QVBoxLayout *leftLayout = new QVBoxLayout();

    m_previewLabel = new QLabel(this);
    m_previewLabel->setFixedSize(240, 240);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setFrameShape(QFrame::Box);
    m_previewLabel->setText(tr("오른쪽 목록에서\n파일을 클릭하세요"));
    leftLayout->addWidget(m_previewLabel, 0, Qt::AlignHCenter);

    m_btnAddToList = new QPushButton(tr("추가 ▼"), this);
    leftLayout->addWidget(m_btnAddToList, 0, Qt::AlignHCenter);

    m_selectedListWidget = new QListWidget(this);
    m_selectedListWidget->setViewMode(QListView::IconMode);
    m_selectedListWidget->setIconSize(QSize(48, 48));
    m_selectedListWidget->setSpacing(6);
    m_selectedListWidget->setResizeMode(QListView::Adjust);
    m_selectedListWidget->setMovement(QListView::Static);
    m_selectedListWidget->setWordWrap(true);
    m_selectedListWidget->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_selectedListWidget->setObjectName("BatchPrintSelectedList");
    leftLayout->addWidget(m_selectedListWidget, 1);

    // [블록Dock 스타일] 출력 목록 관리용 컴팩트 고정폭 버튼
    QHBoxLayout *leftToolRow = new QHBoxLayout();
    m_btnRemove = new QPushButton(tr("-"), this);
    m_btnUp = new QPushButton(tr("▲"), this);
    m_btnDown = new QPushButton(tr("▼"), this);
    m_btnRemove->setFixedWidth(30);
    m_btnUp->setFixedWidth(30);
    m_btnDown->setFixedWidth(30);
    m_btnRemove->setToolTip(tr("출력 목록에서 제거"));
    m_btnUp->setToolTip(tr("위로 이동"));
    m_btnDown->setToolTip(tr("아래로 이동"));
    leftToolRow->addWidget(m_btnRemove);
    leftToolRow->addStretch();
    leftToolRow->addWidget(m_btnUp);
    leftToolRow->addWidget(m_btnDown);
    leftLayout->addLayout(leftToolRow);

    // [수정] 왼쪽(출력 목록)을 넓게, 오른쪽(폴더 브라우저)을 좁게 배치
    centerLayout->addLayout(leftLayout, 2);

    // ==================== 오른쪽: 폴더 브라우저 (블록Dock 스타일) ====================
    QVBoxLayout *rightLayout = new QVBoxLayout();

    QHBoxLayout *typeRow = new QHBoxLayout();
    m_radioCdb = new QRadioButton(tr(".cdb"), this);
    m_radioDwg = new QRadioButton(tr(".dwg"), this);
    m_radioDxf = new QRadioButton(tr(".dxf"), this);
    m_radioCdb->setChecked(true);
    typeRow->addWidget(m_radioCdb);
    typeRow->addWidget(m_radioDwg);
    typeRow->addWidget(m_radioDxf);
    typeRow->addStretch();
    rightLayout->addLayout(typeRow);

    QHBoxLayout *folderRow = new QHBoxLayout();
    m_folderCombo = new QComboBox(this);
    m_folderCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_browseButton = new QPushButton(tr("..."), this);
    m_browseButton->setFixedWidth(30);
    m_clearHistoryButton = new QPushButton(tr("✕"), this);
    m_clearHistoryButton->setFixedWidth(24);
    m_clearHistoryButton->setToolTip(tr("폴더 목록 지우기"));
    folderRow->addWidget(m_folderCombo);
    folderRow->addWidget(m_browseButton);
    folderRow->addWidget(m_clearHistoryButton);
    rightLayout->addLayout(folderRow);

    // 폴더 내 파일명 목록 (미리보기 없음)
    m_sourceListWidget = new QListWidget(this);
    m_sourceListWidget->setViewMode(QListView::ListMode);
    m_sourceListWidget->setObjectName("BatchPrintSourceList");
    rightLayout->addWidget(m_sourceListWidget, 1);

    centerLayout->addLayout(rightLayout, 1); // 왼쪽(2) : 오른쪽(1) 비율

    mainLayout->addLayout(centerLayout, 1);

    // ==================== 하단 확인/취소 ====================
    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch(1);
    m_btnOk = new QPushButton(tr("출력"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);
    bottomLayout->addWidget(m_btnOk);
    bottomLayout->addWidget(m_btnCancel);
    mainLayout->addLayout(bottomLayout);

    // ==================== 시그널 연결 ====================
    connect(m_radioCdb, &QRadioButton::toggled, this, &BatchPrintDialog::onSourceTypeChanged);
    connect(m_radioDwg, &QRadioButton::toggled, this, &BatchPrintDialog::onSourceTypeChanged);
    connect(m_radioDxf, &QRadioButton::toggled, this, &BatchPrintDialog::onSourceTypeChanged);
    connect(m_browseButton, &QPushButton::clicked, this, &BatchPrintDialog::onBrowseFolderClicked);
    connect(m_clearHistoryButton, &QPushButton::clicked, this, &BatchPrintDialog::onClearHistoryClicked);
    connect(m_folderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BatchPrintDialog::onFolderChanged);
    connect(m_sourceListWidget, &QListWidget::itemClicked, this, &BatchPrintDialog::onSourceItemClicked);

    connect(m_btnAddToList, &QPushButton::clicked, this, &BatchPrintDialog::onAddToList);
    connect(m_selectedListWidget, &QListWidget::itemClicked, this, &BatchPrintDialog::onSelectedItemClicked);
    connect(m_btnRemove, &QPushButton::clicked, this, &BatchPrintDialog::onRemoveSelected);
    connect(m_btnUp, &QPushButton::clicked, this, &BatchPrintDialog::onMoveUp);
    connect(m_btnDown, &QPushButton::clicked, this, &BatchPrintDialog::onMoveDown);
    connect(m_btnOk, &QPushButton::clicked, this, &BatchPrintDialog::onAccept);
    connect(m_btnCancel, &QPushButton::clicked, this, &BatchPrintDialog::reject);
}

// ==================== 오른쪽: 폴더 브라우저 ====================

QString BatchPrintDialog::currentExtensionGlob() const {
    if (m_radioDwg->isChecked())
        return QStringLiteral("*.dwg");
    if (m_radioDxf->isChecked())
        return QStringLiteral("*.dxf");
    return QStringLiteral("*.cdb");
}

void BatchPrintDialog::onSourceTypeChanged() {
    refreshSourceList();
}

void BatchPrintDialog::refreshSourceList() {
    m_sourceListWidget->clear();

    if (m_currentFolderPath.isEmpty())
        return;

    QDir dir(m_currentFolderPath);
    if (!dir.exists())
        return;

    QStringList filters;
    filters << currentExtensionGlob();

    QFileInfoList files = dir.entryInfoList(filters, QDir::Files, QDir::Name);
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo &info = files.at(i);
        QListWidgetItem *item = new QListWidgetItem(info.fileName(), m_sourceListWidget);
        item->setData(Qt::UserRole, info.absoluteFilePath());
        item->setToolTip(info.absoluteFilePath());
    }
}

void BatchPrintDialog::onBrowseFolderClicked() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("도면 폴더 선택"), m_currentFolderPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        addFolderToHistory(dir);
    }
}

void BatchPrintDialog::onClearHistoryClicked() {
    m_folderCombo->blockSignals(true);
    m_folderCombo->clear();
    m_folderCombo->blockSignals(false);

    m_currentFolderPath.clear();
    saveFolderHistory();
    m_sourceListWidget->clear();
}

void BatchPrintDialog::onFolderChanged(int index) {
    if (index >= 0) {
        m_currentFolderPath = m_folderCombo->itemData(index).toString();
        refreshSourceList();
    }
}

void BatchPrintDialog::saveFolderHistory() {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    QStringList history;
    for (int i = 0; i < m_folderCombo->count(); ++i) {
        history << m_folderCombo->itemData(i).toString();
    }
    settings.setValue("batchPrintFolders", history);
    settings.setValue("lastBatchPrintFolder", m_currentFolderPath);
}

void BatchPrintDialog::loadFolderHistory() {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    QStringList history = settings.value("batchPrintFolders").toStringList();
    QString lastFolder = settings.value("lastBatchPrintFolder").toString();

    m_folderCombo->blockSignals(true);
    m_folderCombo->clear();

    for (int i = 0; i < history.size(); ++i) {
        const QString &path = history.at(i);
        if (QDir(path).exists()) {
            m_folderCombo->addItem(QDir(path).dirName(), path);
            m_folderCombo->setItemData(m_folderCombo->count() - 1, path, Qt::ToolTipRole);
        }
    }

    // [핵심 수정] 저장된 폴더 기록이 없는 첫 실행 시, 콤보박스가 완전히 비어 있어
    // m_currentFolderPath가 끝까지 빈 문자열로 남고 오른쪽 목록도 채워지지 않는 문제가 있었다.
    // BlockPaletteDock과 동일하게 기본 폴더를 하나 시드해 준다.
    if (m_folderCombo->count() == 0) {
        QString defaultPath = QDir::currentPath() + "/Data";
        if (!QDir(defaultPath).exists())
            defaultPath = QDir::currentPath();
        m_folderCombo->addItem(QDir(defaultPath).dirName(), defaultPath);
        m_folderCombo->setItemData(0, defaultPath, Qt::ToolTipRole);
    }

    int index = m_folderCombo->findData(lastFolder);
    if (index >= 0) {
        m_folderCombo->setCurrentIndex(index);
        m_currentFolderPath = lastFolder;
    } else if (m_folderCombo->count() > 0) {
        m_folderCombo->setCurrentIndex(0);
        m_currentFolderPath = m_folderCombo->itemData(0).toString();
    }
    m_folderCombo->blockSignals(false);

    refreshSourceList();
}

void BatchPrintDialog::addFolderToHistory(const QString &path) {
    int index = m_folderCombo->findData(path);
    if (index >= 0) {
        m_folderCombo->setCurrentIndex(index);
    } else {
        m_folderCombo->insertItem(0, QDir(path).dirName(), path);
        m_folderCombo->setItemData(0, path, Qt::ToolTipRole);
        m_folderCombo->setCurrentIndex(0);
    }
    m_currentFolderPath = path;
    saveFolderHistory();
    refreshSourceList();
}

// ==================== 미리보기 생성 ====================

QPixmap BatchPrintDialog::generatePreview(const QString &filePath) {
    QString ext = QFileInfo(filePath).suffix().toLower();

    // .cdb는 파일에 저장된 미리보기를 우선 사용한다 (전체 로드 없이 즉시 표시 가능)
    if (ext == QLatin1String("cdb") && m_ctx && m_ctx->fileManager()) {
        QImage stored = m_ctx->fileManager()->loadThumbnail(filePath);
        if (!stored.isNull()) {
            return QPixmap::fromImage(stored);
        }
    }

    // 저장된 미리보기가 없거나 .dwg/.dxf인 경우, 임시 도면 창에 실제로 로드해서 즉석 렌더링한다.
    if (!m_ctx || !m_ctx->mdiArea() || !m_ctx->fileManager())
        return QPixmap();

    DrawSubWindow *originalActive = m_ctx->activeDrawWindow();

    // [핵심] LayerManager는 "현재 활성 서브윈도우"가 각자 소유하므로, 이 임시 창을
    // 반드시 활성화한 뒤에 로드해야 원래 활성 창의 레이어가 덮어써지는 사고를 막을 수 있다.
    DrawSubWindow *tempWindow = new DrawSubWindow();
    m_ctx->addSubWindow(tempWindow);
    m_ctx->mdiArea()->setActiveSubWindow(tempWindow);

    // [핵심] AcadrustImporter/DxfManager는 레이어 등록 시 scene->context()->layerManager()로
    // "현재 활성 창"을 다시 찾는데, setActiveSubWindow() 직후에도 실제 활성 창 전환이
    // 이벤트 큐를 통해 비동기로 반영될 수 있다(filecommands.cpp의 일괄출력 루프에서
    // 발견된 것과 동일한 문제). 이걸 비워주지 않으면 레이어가 엉뚱한 LayerManager에
    // 등록되어 이 tempWindow의 getLayerStyle() 캐시가 비어, 미리보기가 레이어색 대신
    // 단일 색(회색 폴백)으로만 렌더링된다. 한 번으로 안 빠질 수 있어 몇 차례 반복한다.
    for (int retry = 0; retry < 10 && m_ctx->activeDrawWindow() != tempWindow; ++retry) {
        QApplication::processEvents();
    }
    if (m_ctx->activeDrawWindow() != tempWindow) {
        tempWindow->close();
        if (originalActive) {
            m_ctx->mdiArea()->setActiveSubWindow(originalActive);
        }
        return QPixmap();
    }

    bool loaded = false;
    if (ext == QLatin1String("dwg")) {
        AcadrustImporter importer;
        loaded = importer.importFromDwg(filePath, tempWindow->scene(), nullptr, nullptr);
    } else if (ext == QLatin1String("dxf")) {
        DxfManager dxfManager(this);
        loaded = dxfManager.importFromDxf(filePath, tempWindow->scene());
    } else {
        loaded = m_ctx->fileManager()->loadDrawing(filePath, tempWindow->scene());
    }

    QPixmap result;
    if (loaded) {
        QByteArray pngData = m_ctx->fileManager()->capturePreview(tempWindow->scene());
        if (!pngData.isEmpty()) {
            result.loadFromData(pngData, "PNG");
        }
    }

    tempWindow->close(); // WA_DeleteOnClose 속성으로 자동 소멸됨

    if (originalActive) {
        m_ctx->mdiArea()->setActiveSubWindow(originalActive);
    }

    return result;
}

void BatchPrintDialog::onSourceItemClicked(QListWidgetItem *item) {
    if (!item)
        return;

    QString path = item->data(Qt::UserRole).toString();
    QPixmap preview = generatePreview(path);

    m_lastPreviewedPath = path;
    m_lastPreviewedPixmap = preview;

    if (preview.isNull()) {
        m_previewLabel->setText(tr("미리보기를 생성할 수 없습니다."));
    } else {
        m_previewLabel->setPixmap(preview.scaled(
            m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

// ==================== 왼쪽: 출력 목록 ====================

void BatchPrintDialog::onAddToList() {
    if (m_lastPreviewedPath.isEmpty()) {
        QMessageBox::information(this, tr("알림"),
            tr("먼저 오른쪽 목록에서 파일을 클릭해 미리보기를 확인하세요."));
        return;
    }

    // 이미 추가된 파일은 중복 추가하지 않음
    for (int r = 0; r < m_selectedListWidget->count(); ++r) {
        if (m_selectedListWidget->item(r)->data(Qt::UserRole).toString() == m_lastPreviewedPath)
            return;
    }

    QIcon icon;
    if (!m_lastPreviewedPixmap.isNull()) {
        icon = QIcon(m_lastPreviewedPixmap.scaled(48, 48, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
        icon = style()->standardIcon(QStyle::SP_FileIcon);
    }

    QListWidgetItem *item = new QListWidgetItem(icon, QFileInfo(m_lastPreviewedPath).fileName(), m_selectedListWidget);
    item->setData(Qt::UserRole, m_lastPreviewedPath);
    // 큰 미리보기 이미지를 함께 저장해 둔다 (이 항목을 다시 클릭했을 때 재생성 없이 즉시 동기화하기 위함)
    item->setData(PreviewPixmapRole, m_lastPreviewedPixmap);
    item->setToolTip(m_lastPreviewedPath);
    item->setTextAlignment(Qt::AlignCenter);
}

void BatchPrintDialog::onSelectedItemClicked(QListWidgetItem *item) {
    if (!item)
        return;

    QString path = item->data(Qt::UserRole).toString();
    QPixmap preview = item->data(PreviewPixmapRole).value<QPixmap>();

    // 출력 목록의 작은 아이콘을 클릭하면, 추가할 때 저장해 둔 큰 미리보기 이미지를
    // 상단 미리보기 영역에 그대로 동기화해서 보여준다 (재생성하지 않아 즉시 표시됨).
    m_lastPreviewedPath = path;
    m_lastPreviewedPixmap = preview;

    if (preview.isNull()) {
        m_previewLabel->setText(tr("미리보기를 생성할 수 없습니다."));
    } else {
        m_previewLabel->setPixmap(preview.scaled(
            m_previewLabel->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
}

void BatchPrintDialog::onRemoveSelected() {
    QList<QListWidgetItem *> selectedItems = m_selectedListWidget->selectedItems();
    for (int i = 0; i < selectedItems.size(); ++i) {
        delete m_selectedListWidget->takeItem(m_selectedListWidget->row(selectedItems.at(i)));
    }
}

void BatchPrintDialog::onMoveUp() {
    int row = m_selectedListWidget->currentRow();
    if (row <= 0)
        return;

    QListWidgetItem *item = m_selectedListWidget->takeItem(row);
    m_selectedListWidget->insertItem(row - 1, item);
    m_selectedListWidget->setCurrentRow(row - 1);
}

void BatchPrintDialog::onMoveDown() {
    int row = m_selectedListWidget->currentRow();
    if (row < 0 || row >= m_selectedListWidget->count() - 1)
        return;

    QListWidgetItem *item = m_selectedListWidget->takeItem(row);
    m_selectedListWidget->insertItem(row + 1, item);
    m_selectedListWidget->setCurrentRow(row + 1);
}

void BatchPrintDialog::onAccept() {
    if (m_selectedListWidget->count() == 0) {
        QMessageBox::information(this, tr("알림"), tr("출력할 도면 파일을 1개 이상 추가하세요."));
        return;
    }
    accept();
}

QStringList BatchPrintDialog::filePaths() const {
    QStringList result;
    for (int i = 0; i < m_selectedListWidget->count(); ++i) {
        result.append(m_selectedListWidget->item(i)->data(Qt::UserRole).toString());
    }
    return result;
}
