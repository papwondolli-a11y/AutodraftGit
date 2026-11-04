#include "newfromtemplatedialog.h"
#include "iappcontext.h"
#include "templatemanager.h"

#include <QAbstractItemView>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

// 리스트 아이템의 Qt::UserRole 에 템플릿 절대 경로를 담는다.
// 빈 문자열이면 "빈 도면".
static const int kPathRole = Qt::UserRole + 1;

NewFromTemplateDialog::NewFromTemplateDialog(IAppContext *ctx, QWidget *parent)
    : QDialog(parent), m_ctx(ctx), m_list(nullptr), m_browseButton(nullptr),
      m_newButton(nullptr), m_replaceButton(nullptr), m_editButton(nullptr),
      m_deleteButton(nullptr), m_cancelButton(nullptr), m_action(Action::None) {
    setWindowTitle(tr("템플릿 보기"));
    resize(560, 480);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QLabel *hint = new QLabel(
        tr("템플릿의 레이어·치수 스타일·축척·도곽이 그대로 복제됩니다.\n"
           "만들기: 새 빈 원형도면(새 창) · 교체: 선택 항목을 기본 원형도면으로 설정\n"
           "편집: 템플릿 파일 자체 수정 · 삭제: 템플릿 제거"),
        this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    m_list = new QListWidget(this);
    m_list->setViewMode(QListView::IconMode);
    m_list->setIconSize(QSize(160, 120));
    m_list->setGridSize(QSize(180, 160));
    m_list->setResizeMode(QListView::Adjust);
    m_list->setMovement(QListView::Static);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_list, 1);

    QHBoxLayout *optRow = new QHBoxLayout();
    m_browseButton = new QPushButton(tr("찾아보기..."), this);
    optRow->addWidget(m_browseButton);
    optRow->addStretch(1);
    mainLayout->addLayout(optRow);

    QHBoxLayout *btnRow = new QHBoxLayout();
    btnRow->addStretch(1);
    m_newButton     = new QPushButton(tr("만들기"), this);
    m_replaceButton = new QPushButton(tr("교체"), this);
    m_editButton    = new QPushButton(tr("편집"), this);
    m_deleteButton  = new QPushButton(tr("삭제"), this);
    m_cancelButton  = new QPushButton(tr("취소"), this);
    m_replaceButton->setDefault(true);
    btnRow->addWidget(m_newButton);
    btnRow->addWidget(m_replaceButton);
    btnRow->addWidget(m_editButton);
    btnRow->addWidget(m_deleteButton);
    btnRow->addWidget(m_cancelButton);
    mainLayout->addLayout(btnRow);

    connect(m_browseButton, &QPushButton::clicked, this,
            &NewFromTemplateDialog::onBrowse);
    connect(m_newButton, &QPushButton::clicked, this,
            &NewFromTemplateDialog::onNewClicked);
    connect(m_replaceButton, &QPushButton::clicked, this,
            &NewFromTemplateDialog::onReplaceClicked);
    connect(m_editButton, &QPushButton::clicked, this,
            &NewFromTemplateDialog::onEditClicked);
    connect(m_deleteButton, &QPushButton::clicked, this,
            &NewFromTemplateDialog::onDeleteClicked);
    connect(m_cancelButton, &QPushButton::clicked, this,
            &NewFromTemplateDialog::reject);
    connect(m_list, &QListWidget::itemDoubleClicked, this,
            &NewFromTemplateDialog::onItemDoubleClicked);
    connect(m_list, &QListWidget::itemSelectionChanged, this,
            &NewFromTemplateDialog::onSelectionChanged);

    reloadList();
}

NewFromTemplateDialog::~NewFromTemplateDialog() {}

void NewFromTemplateDialog::reloadList() {
    m_list->clear();

    // 0) 항상 맨 위에 "빈 도면"
    QListWidgetItem *blank =
        new QListWidgetItem(QIcon(QStringLiteral(":/icons/new.png")),
                            tr("빈 도면"), m_list);
    blank->setData(kPathRole, QString());

    // 1) data 폴더 스캔
    const QList<TemplateInfo> templates = TemplateManager::availableTemplates();
    const QString defPath = TemplateManager::defaultTemplatePath();

    for (int i = 0; i < templates.size(); ++i) {
        const TemplateInfo &info = templates.at(i);
        QIcon icon;
        if (!info.thumbnail.isNull())
            icon = QIcon(QPixmap::fromImage(info.thumbnail));
        else
            icon = QIcon(QStringLiteral(":/icons/new.png"));

        QString label = info.name;
        if (!defPath.isEmpty() && QFileInfo(defPath) == QFileInfo(info.path))
            label += tr("  (기본)");

        QListWidgetItem *item = new QListWidgetItem(icon, label, m_list);
        item->setData(kPathRole, info.path);
        item->setToolTip(info.path);
    }

    // 2) "찾아보기..."로 고른 폴더 밖 경로가 있으면 뒤에 붙인다
    if (!m_browsedPath.isEmpty() && QFileInfo::exists(m_browsedPath)) {
        QListWidgetItem *item = new QListWidgetItem(
            QIcon(QStringLiteral(":/icons/open.png")),
            QFileInfo(m_browsedPath).completeBaseName(), m_list);
        item->setData(kPathRole, m_browsedPath);
        item->setToolTip(m_browsedPath);
        m_list->setCurrentItem(item);
    }

    if (!m_list->currentItem() && m_list->count() > 0)
        m_list->setCurrentRow(0);

    onSelectionChanged();
}

bool NewFromTemplateDialog::currentIsRealFile() const {
    const QString path = selectedTemplatePath();
    return !path.isEmpty() && QFileInfo::exists(path);
}

void NewFromTemplateDialog::onBrowse() {
    const QString start = TemplateManager::templateDir();
    const QString path = QFileDialog::getOpenFileName(
        this, tr("템플릿 선택"), start,
        tr("CAD 템플릿/도면 (*.cdt *.cdb);;모든 파일 (*)"));
    if (path.isEmpty())
        return;
    m_browsedPath = path;
    reloadList();
}

void NewFromTemplateDialog::onNewClicked() {
    // 목록 선택과 무관하게 항상 새로운 빈 원형도면을 만든다.
    m_action = Action::CreateNew;
    accept();
}

void NewFromTemplateDialog::onReplaceClicked() {
    if (!m_list->currentItem())
        return;
    // 새 창은 열지 않고, 선택 항목을 기본 원형도면으로 설정만 한다.
    m_action = Action::SetDefault;
    accept();
}

void NewFromTemplateDialog::onEditClicked() {
    if (!currentIsRealFile()) {
        QMessageBox::information(this, tr("편집"),
                                tr("편집할 템플릿 파일을 선택하세요."));
        return;
    }
    m_action = Action::Edit;
    accept();
}

void NewFromTemplateDialog::onDeleteClicked() {
    if (!currentIsRealFile()) {
        QMessageBox::information(this, tr("삭제"),
                                tr("삭제할 템플릿 파일을 선택하세요."));
        return;
    }
    const QString path = selectedTemplatePath();
    const QMessageBox::StandardButton reply = QMessageBox::warning(
        this, tr("템플릿 삭제"),
        tr("템플릿 '%1' 을(를) 삭제할까요?\n이 작업은 되돌릴 수 없습니다.")
            .arg(QFileInfo(path).completeBaseName()),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (reply != QMessageBox::Yes)
        return;

    if (!QFile::remove(path)) {
        QMessageBox::critical(this, tr("템플릿 삭제"),
                              tr("파일을 삭제하지 못했습니다:\n%1").arg(path));
        return;
    }

    // 방금 지운 게 기본 템플릿이었다면 기본값도 해제한다.
    const QString defPath = TemplateManager::defaultTemplatePath();
    if (!defPath.isEmpty() && QFileInfo(defPath) == QFileInfo(path))
        TemplateManager::setDefaultTemplatePath(QString());
    if (!m_browsedPath.isEmpty() && QFileInfo(m_browsedPath) == QFileInfo(path))
        m_browsedPath.clear();

    reloadList();

    // 목록/기본값이 바뀌었으므로 대시보드 "원형도면" 콤보도 즉시 갱신한다.
    if (m_ctx)
        m_ctx->refreshDashboardTemplates();
}

void NewFromTemplateDialog::onItemDoubleClicked(QListWidgetItem *item) {
    if (!item)
        return;
    // 더블클릭 = "교체" (선택 항목을 기본 원형도면으로 설정)
    m_action = Action::SetDefault;
    accept();
}

void NewFromTemplateDialog::onSelectionChanged() {
    const bool hasItem = (m_list->currentItem() != nullptr);
    const bool realFile = currentIsRealFile();

    // "만들기"는 목록 선택과 무관하게 항상 사용 가능.
    if (m_replaceButton)
        m_replaceButton->setEnabled(hasItem);
    if (m_editButton)
        m_editButton->setEnabled(realFile);
    if (m_deleteButton)
        m_deleteButton->setEnabled(realFile);
}

QString NewFromTemplateDialog::selectedTemplatePath() const {
    QListWidgetItem *item = m_list ? m_list->currentItem() : nullptr;
    if (!item)
        return QString();
    return item->data(kPathRole).toString();
}
