#include "blockpalettedock.h"
#include "blockitem.h"
#include "caditem.h"
#include "filemanager.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "mygraphicsscene.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>

BlockPaletteDock::BlockPaletteDock(IAppContext *ctx, QWidget *parent)
    : QDockWidget(tr("블록 목록"), parent), m_ctx(ctx), m_layerManager(nullptr) {
    setObjectName("BlockPaletteDock");

    QWidget *content = new QWidget(this);
    content->setObjectName("BlockPaletteDockContent");
    QVBoxLayout *layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 4, 4, 4);

    // 1. 소스 선택 (라이브러리 / 현재 도면) 및 보기 모드 (콤보박스)
    QHBoxLayout *topRowLayout = new QHBoxLayout();
    m_radioLibrary = new QRadioButton(tr("라이브러리"), this);
    m_radioCurrent = new QRadioButton(tr("현재 도면"), this);
    m_radioLibrary->setChecked(true);

    m_viewModeCombo = new QComboBox(this);
    m_viewModeCombo->addItem(tr("미리보기"), 0);
    m_viewModeCombo->addItem(tr("간단히 보기"), 1);

    topRowLayout->addWidget(m_radioLibrary);
    topRowLayout->addWidget(m_radioCurrent);
    topRowLayout->addStretch();
    topRowLayout->addWidget(m_viewModeCombo);
    layout->addLayout(topRowLayout);

    // 2. 상단 폴더 선택 영역
    QHBoxLayout *topLayout = new QHBoxLayout();
    m_folderCombo = new QComboBox(this);
    m_folderCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    m_browseButton = new QPushButton(tr("..."), this);
    m_browseButton->setFixedWidth(30);

    m_clearHistoryButton = new QPushButton(tr("✕"), this);
    m_clearHistoryButton->setFixedWidth(24);
    m_clearHistoryButton->setToolTip(tr("폴더 목록 지우기"));

    topLayout->addWidget(m_folderCombo);
    topLayout->addWidget(m_browseButton);
    topLayout->addWidget(m_clearHistoryButton);
    layout->addLayout(topLayout);

    // 3. 레이어 설정 행
    QHBoxLayout *layerLayout = new QHBoxLayout();
    m_layerLabel = new QLabel(tr(" 레이어 설정 : "), this);
    m_layerCombo = new QComboBox(this);
    m_layerCombo->setObjectName("TopLayerToolCombo");
    m_layerCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_layerCombo->setMaximumHeight(33);
    m_layerCombo->setEditable(true);
    if (m_layerCombo->lineEdit()) {
        m_layerCombo->lineEdit()->setReadOnly(true);
        m_layerCombo->lineEdit()->setAlignment(Qt::AlignCenter);
        m_layerCombo->lineEdit()->setProperty("noRedirect", true);
    }
    layerLayout->addWidget(m_layerLabel);
    layerLayout->addWidget(m_layerCombo);
    layout->addLayout(layerLayout);

    m_listWidget = new QListWidget(this);
    m_listWidget->setViewMode(QListView::IconMode);
    m_listWidget->setIconSize(QSize(80, 80));
    m_listWidget->setSpacing(10);
    m_listWidget->setResizeMode(QListView::Adjust);
    m_listWidget->setMovement(QListView::Static);
    m_listWidget->setWordWrap(true);

    // [수정] 하드코딩된 스타일시트를 제거하고 ObjectName 부여
    m_listWidget->setObjectName("BlockListWidget");

    layout->addWidget(m_listWidget);
    setWidget(content);

    connect(m_listWidget, &QListWidget::itemClicked, this,
            &BlockPaletteDock::onItemClicked);
    connect(m_listWidget, &QListWidget::itemDoubleClicked, this,
            &BlockPaletteDock::onItemDoubleClicked);
    connect(m_browseButton, &QPushButton::clicked, this,
            &BlockPaletteDock::onBrowseButtonClicked);
    connect(m_clearHistoryButton, &QPushButton::clicked, this,
            &BlockPaletteDock::onClearHistoryClicked);
    connect(m_folderCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BlockPaletteDock::onFolderChanged);

    connect(m_radioLibrary, &QRadioButton::toggled, this,
            &BlockPaletteDock::onSourceChanged);
    connect(m_radioCurrent, &QRadioButton::toggled, this,
            &BlockPaletteDock::onSourceChanged);
    connect(m_viewModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BlockPaletteDock::onViewModeChanged);
    connect(m_layerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &BlockPaletteDock::onLayerComboChanged);

    if (m_ctx && m_ctx->fileManager()) {
        connect(m_ctx->fileManager(), &FileManager::libraryChanged, this,
                &BlockPaletteDock::refresh);
    }

    // LayerManager 연결은 setLayerManager()에서 담당
    // (활성 서브윈도우 전환 시 mainwindow가 호출)

    loadFolderLayerMap();
    loadFolderHistory();
    refreshLayers();
}

void BlockPaletteDock::setLayerManager(LayerManager *mgr) {
    if (m_layerManager) {
        disconnect(m_layerManager, &LayerManager::layerPropertyChanged,
                   this, &BlockPaletteDock::refreshLayers);
        disconnect(m_layerManager, &QAbstractItemModel::rowsInserted,
                   this, &BlockPaletteDock::refreshLayers);
        disconnect(m_layerManager, &QAbstractItemModel::rowsRemoved,
                   this, &BlockPaletteDock::refreshLayers);
        disconnect(m_layerManager, &QAbstractItemModel::dataChanged,
                   this, &BlockPaletteDock::refreshLayers);
    }

    m_layerManager = mgr;

    if (m_layerManager) {
        connect(m_layerManager, &LayerManager::layerPropertyChanged,
                this, &BlockPaletteDock::refreshLayers);
        connect(m_layerManager, &QAbstractItemModel::rowsInserted,
                this, &BlockPaletteDock::refreshLayers);
        connect(m_layerManager, &QAbstractItemModel::rowsRemoved,
                this, &BlockPaletteDock::refreshLayers);
        connect(m_layerManager, &QAbstractItemModel::dataChanged,
                this, &BlockPaletteDock::refreshLayers);
    }

    refreshLayers();
}

void BlockPaletteDock::refreshLayers() {
    if (!m_layerManager)
        return;

    QString current = selectedLayerName();

    m_layerCombo->blockSignals(true);
    m_layerCombo->clear();

    // 첫 항목: 원본 레이어 유지 (빈 문자열로 저장)
    m_layerCombo->addItem(tr("기본값 (원본)"), QString());

    QList<Layer> layers = m_layerManager->getAllLayers();
    for (int i = 0; i < layers.size(); ++i) {
        const Layer &layer = layers.at(i);
        QPixmap px(16, 16);
        px.fill(layer.color());
        m_layerCombo->addItem(QIcon(px), layer.name(), layer.name());
    }

    // 현재 폴더에 저장된 레이어 복원 (없으면 이전 선택값, 그것도 없으면 기본값)
    QString target = m_folderLayerMap.value(m_currentFolderPath, current);
    int idx = target.isEmpty() ? 0 : m_layerCombo->findData(target);
    m_layerCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    m_layerCombo->blockSignals(false);
}

QString BlockPaletteDock::selectedLayerName() const {
    if (!m_layerCombo || m_layerCombo->currentIndex() <= 0)
        return QString();
    return m_layerCombo->currentData().toString();
}

void BlockPaletteDock::activateFolder(const QString &path) {
    m_radioLibrary->setChecked(true);

    int index = m_folderCombo->findData(path);
    if (index >= 0) {
        m_folderCombo->setCurrentIndex(index);
    } else {
        addFolderToHistory(path);
    }
}

void BlockPaletteDock::refresh() {
    if (!m_ctx || !m_ctx->fileManager())
        return;

    m_listWidget->clear();

    if (m_radioLibrary->isChecked()) {
        if (m_currentFolderPath.isEmpty())
            return;

        QDir dir(m_currentFolderPath);
        if (!dir.exists())
            return;

        QStringList filters;
        filters << "*.cdb";
        QFileInfoList files = dir.entryInfoList(filters, QDir::Files);

        // [수정] detach 경고 방지를 위한 인덱스 기반 루프 사용
        for (int i = 0; i < files.size(); ++i) {
            QFileInfo info = files.at(i);
            QString fullPath = info.absoluteFilePath();
            QIcon icon;

            if (m_viewModeCombo->currentIndex() == 0) {
                QImage thumb = m_ctx->fileManager()->loadThumbnail(fullPath);
                if (!thumb.isNull()) {
                    icon = QIcon(QPixmap::fromImage(thumb));
                } else {
                    icon = style()->standardIcon(QStyle::SP_FileIcon);
                }
            }

            QListWidgetItem *item =
                new QListWidgetItem(icon, info.baseName(), m_listWidget);
            item->setData(Qt::UserRole, fullPath);
            item->setToolTip(fullPath);
            item->setTextAlignment(Qt::AlignCenter);
            m_listWidget->addItem(item);
        }
    } else {
        MyGraphicsScene *scene = m_ctx->activeScene();
        if (!scene)
            return;

        QMap<QString, BlockItem *> uniqueBlocks;
        QList<QGraphicsItem*> items = scene->items();
        // [수정] 인덱스 기반 루프로 변경
        for (int i = 0; i < items.size(); ++i) {
            if (BlockItem *block = dynamic_cast<BlockItem *>(items.at(i))) {
                if (!uniqueBlocks.contains(block->blockName())) {
                    uniqueBlocks.insert(block->blockName(), block);
                }
            }
        }

        for (auto it = uniqueBlocks.begin(); it != uniqueBlocks.end(); ++it) {
            QString name = it.key();
            BlockItem *sourceBlock = it.value();

            QIcon icon;
            if (m_viewModeCombo->currentIndex() == 0) {
                icon = renderBlockIcon(sourceBlock);
            }

            QListWidgetItem *item = new QListWidgetItem(icon, name, m_listWidget);
            item->setData(Qt::UserRole, "internal:" + name);
            item->setToolTip(tr("Current Drawing Block: %1").arg(name));
            item->setTextAlignment(Qt::AlignCenter);
            m_listWidget->addItem(item);
        }
    }
}

QIcon BlockPaletteDock::renderBlockIcon(BlockItem *block) {
    if (!block)
        return QIcon();

    QGraphicsScene tempScene;
    QList<CadItem*> blockItems = block->getItems();
    // [수정] 인덱스 기반 루프로 변경
    for (int i = 0; i < blockItems.size(); ++i) {
        CadItem *cloned = blockItems.at(i)->clone();
        if (cloned) {
            tempScene.addItem(cloned);
        }
    }

    QRectF rect = tempScene.itemsBoundingRect();
    if (rect.isEmpty() || !rect.isValid()) {
        return style()->standardIcon(QStyle::SP_FileIcon);
    }

    double margin = qMax(rect.width(), rect.height()) * 0.05;
    rect.adjust(-margin, -margin, margin, margin);

    QImage image(128, 128, QImage::Format_ARGB32);
    image.fill(Qt::white);

    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        tempScene.render(&painter, image.rect(), rect);
    }

    image = image.flipped(Qt::Vertical);

    return QIcon(QPixmap::fromImage(image));
}

void BlockPaletteDock::onSourceChanged() {
    bool libraryMode = m_radioLibrary->isChecked();
    m_folderCombo->setVisible(libraryMode);
    m_browseButton->setVisible(libraryMode);
    m_clearHistoryButton->setVisible(libraryMode);
    refresh();
}

void BlockPaletteDock::onViewModeChanged() {
    if (m_viewModeCombo->currentIndex() == 0) {
        m_listWidget->setViewMode(QListView::IconMode);
        m_listWidget->setIconSize(QSize(80, 80));
        m_listWidget->setWordWrap(true);
    } else {
        m_listWidget->setViewMode(QListView::ListMode);
        m_listWidget->setIconSize(QSize(0, 0));
        m_listWidget->setWordWrap(false);
    }
    refresh();
}

void BlockPaletteDock::onItemClicked(QListWidgetItem *item) {
    if (item) {
        QString path = item->data(Qt::UserRole).toString();
        emit blockClicked(path);
    }
}

void BlockPaletteDock::onItemDoubleClicked(QListWidgetItem *item) {
    if (item) {
        QString path = item->data(Qt::UserRole).toString();
        emit blockDoubleClicked(path);
    }
}

void BlockPaletteDock::onBrowseButtonClicked() {
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("블록 폴더 선택"), m_currentFolderPath,
        QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);

    if (!dir.isEmpty()) {
        addFolderToHistory(dir);
    }
}

void BlockPaletteDock::onClearHistoryClicked() {
    m_folderCombo->blockSignals(true);
    m_folderCombo->clear();
    m_folderCombo->blockSignals(false);

    m_currentFolderPath.clear();
    saveFolderHistory();
    m_listWidget->clear();
}

void BlockPaletteDock::onFolderChanged(int index) {
    if (index >= 0) {
        m_currentFolderPath = m_folderCombo->itemData(index).toString();
        applyFolderLayer(m_currentFolderPath);
        refresh();
    }
}

void BlockPaletteDock::onLayerComboChanged(int index) {
    Q_UNUSED(index)
    if (m_currentFolderPath.isEmpty())
        return;
    // 현재 폴더에 선택된 레이어를 저장
    m_folderLayerMap[m_currentFolderPath] = selectedLayerName();
    saveFolderLayerMap();
}

void BlockPaletteDock::applyFolderLayer(const QString &folderPath) {
    m_layerCombo->blockSignals(true);
    if (m_folderLayerMap.contains(folderPath)) {
        QString saved = m_folderLayerMap.value(folderPath);
        int idx = saved.isEmpty() ? 0 : m_layerCombo->findData(saved);
        m_layerCombo->setCurrentIndex(idx >= 0 ? idx : 0);
    } else {
        m_layerCombo->setCurrentIndex(0); // 기본값(원본)
    }
    m_layerCombo->blockSignals(false);
}

void BlockPaletteDock::saveFolderLayerMap() {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    settings.beginGroup("blockPaletteFolderLayers");
    settings.remove(""); // 기존 항목 전체 삭제 후 재작성
    QMap<QString,QString>::const_iterator it = m_folderLayerMap.constBegin();
    for (; it != m_folderLayerMap.constEnd(); ++it) {
        // 키에 '/'가 포함되므로 Base64로 인코딩
        QString encodedKey = QString::fromLatin1(it.key().toUtf8().toBase64());
        settings.setValue(encodedKey, it.value());
    }
    settings.endGroup();
}

void BlockPaletteDock::loadFolderLayerMap() {
    m_folderLayerMap.clear();
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    settings.beginGroup("blockPaletteFolderLayers");
    QStringList keys = settings.childKeys();
    for (int i = 0; i < keys.size(); ++i) {
        QString folderPath = QString::fromUtf8(QByteArray::fromBase64(keys.at(i).toLatin1()));
        QString layerName  = settings.value(keys.at(i)).toString();
        m_folderLayerMap[folderPath] = layerName;
    }
    settings.endGroup();
}

void BlockPaletteDock::saveFolderHistory() {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    QStringList history;
    for (int i = 0; i < m_folderCombo->count(); ++i) {
        history << m_folderCombo->itemData(i).toString();
    }
    settings.setValue("blockPaletteFolders", history);
    settings.setValue("lastBlockPaletteFolder", m_currentFolderPath);
}

void BlockPaletteDock::loadFolderHistory() {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    QStringList history = settings.value("blockPaletteFolders").toStringList();
    QString lastFolder = settings.value("lastBlockPaletteFolder").toString();

    m_folderCombo->blockSignals(true);
    m_folderCombo->clear();

    if (history.isEmpty()) {
        QString defaultPath = QDir::currentPath() + "/Data";
        if (!QDir(defaultPath).exists())
            defaultPath = QDir::currentPath();
        addFolderToHistory(defaultPath);
    } else {
        // [수정] detach 경고 방지를 위한 인덱스 기반 루프 사용
        for (int i = 0; i < history.size(); ++i) {
            QString path = history.at(i);
            if (QDir(path).exists()) {
                m_folderCombo->addItem(QDir(path).dirName(), path);
                m_folderCombo->setItemData(m_folderCombo->count() - 1, path,
                                           Qt::ToolTipRole);
            }
        }
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

    refresh();
}

void BlockPaletteDock::addFolderToHistory(const QString &path) {
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
    refresh();
}