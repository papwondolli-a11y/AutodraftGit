#include "gridcentersetupdialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFont>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QRadioButton>
#include <QTextStream>
#include <QVBoxLayout>

namespace {
// data 폴더 위치 탐색: 1) exe 옆 data, 2) 상위 폴더의 data(qmake debug/release 빌드),
// 3) 소스 루트의 data(개발 환경, APP_SOURCE_PATH)
QString resolveDataDir() {
    QString dir = QCoreApplication::applicationDirPath() + "/data";
    if (!QDir(dir).exists()) {
        QString alt = QDir::cleanPath(QCoreApplication::applicationDirPath() + "/../data");
        if (QDir(alt).exists()) {
            dir = alt;
        }
#ifdef APP_SOURCE_PATH
        else {
            QString devDir = QDir::cleanPath(APP_SOURCE_PATH) + QDir::separator() + "data";
            if (QDir(devDir).exists())
                dir = devDir;
        }
#endif
    }
    return dir;
}

// .cen 파일에서 Y=/X= 값을 읽어온다. 실패 시 false.
bool readCenFile(const QString &filePath, QString &yOut, QString &xOut) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        int eq = line.indexOf('=');
        if (eq < 0)
            continue;
        QString key = line.left(eq).trimmed().toUpper();
        QString value = line.mid(eq + 1).trimmed();
        if (key == "Y")
            yOut = value;
        else if (key == "X")
            xOut = value;
    }
    return true;
}

const int YRole = Qt::UserRole;
const int XRole = Qt::UserRole + 1;
}

GridCenterSetupDialog::GridCenterSetupDialog(const QString &ySpacing, const QString &xSpacing,
                                              const GridDimFlags &dimFlags,
                                              const GridBubbleFlags &bubbleFlags,
                                              QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("중심선 간격 설정"));
    setModal(true);

    // ── 오른쪽: 중심선 간격 입력 ─────────────────────────────────────────────────
    m_editYSpacing = new QLineEdit(ySpacing, this);
    m_editYSpacing->setPlaceholderText(tr("예: 3000,3000,3000"));

    m_editXSpacing = new QLineEdit(xSpacing, this);
    m_editXSpacing->setPlaceholderText(tr("예: 1500,2000,2500"));

    // ── 왼쪽: 저장된 .cen 파일 목록 ─────────────────────────────────────────────
    m_fileList = new QListWidget(this);
    m_fileList->setMinimumWidth(90);
    m_fileList->setMaximumWidth(110);

    m_btnDelete = new QPushButton(tr("삭제"), this);

    QLabel *hint = new QLabel(
        tr("콤마로 구분해 간격을 입력해두면, 도구 실행 중 Enter만 눌러도\n"
           "이 값이 자동으로 적용됩니다. 비워두면 중앙에 1개만 생성됩니다."), this);
    hint->setWordWrap(true);
    QFont hintFont = hint->font();
    hintFont.setPointSizeF(hintFont.pointSizeF() * 0.85);
    hint->setFont(hintFont);

    QFormLayout *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(tr("세로축 간격 :"), m_editYSpacing);
    form->addRow(tr("가로축 간격 :"), m_editXSpacing);
    form->addRow(QString(), hint);

    QGroupBox *group = new QGroupBox(tr("중심선 간격"), this);
    group->setLayout(form);

    m_btnSave = new QPushButton(tr("저장하기"), this);

    // ── 치수선 방향 선택 ─────────────────────────────────────────────────────────
    m_chkTop    = new QCheckBox(tr("상단"), this);
    m_chkBottom = new QCheckBox(tr("하단"), this);
    m_chkRight  = new QCheckBox(tr("우측"), this);
    m_chkLeft   = new QCheckBox(tr("좌측"), this);
    m_chkTotal  = new QCheckBox(tr("합계 치수선"), this);
    m_chkDimNone = new QCheckBox(tr("없음"), this);
    m_chkTop->setChecked(dimFlags.top);
    m_chkBottom->setChecked(dimFlags.bottom);
    m_chkLeft->setChecked(dimFlags.left);
    m_chkRight->setChecked(dimFlags.right);
    m_chkTotal->setChecked(dimFlags.total);

    QHBoxLayout *dimRow = new QHBoxLayout;
    dimRow->addWidget(m_chkTop);
    dimRow->addWidget(m_chkBottom);
    dimRow->addWidget(m_chkRight);
    dimRow->addWidget(m_chkLeft);

    QHBoxLayout *dimTotalRow = new QHBoxLayout;
    dimTotalRow->addWidget(m_chkTotal);
    dimTotalRow->addWidget(m_chkDimNone);

    QVBoxLayout *dimLayout = new QVBoxLayout;
    dimLayout->addLayout(dimRow);
    dimLayout->addLayout(dimTotalRow);

    QGroupBox *dimGroup = new QGroupBox(tr("치수선"), this);
    dimGroup->setLayout(dimLayout);

    connect(m_chkDimNone, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_chkTop->setChecked(false);
            m_chkBottom->setChecked(false);
            m_chkLeft->setChecked(false);
            m_chkRight->setChecked(false);
            m_chkTotal->setChecked(false);
        }
        m_chkTop->setEnabled(!checked);
        m_chkBottom->setEnabled(!checked);
        m_chkLeft->setEnabled(!checked);
        m_chkRight->setEnabled(!checked);
        m_chkTotal->setEnabled(!checked);
    });

    // ── 주열번호(그리드 버블) 방향/라벨 선택 ────────────────────────────────────────
    m_chkBubbleTop    = new QCheckBox(tr("상단"), this);
    m_chkBubbleBottom = new QCheckBox(tr("하단"), this);
    m_chkBubbleRight  = new QCheckBox(tr("우측"), this);
    m_chkBubbleLeft   = new QCheckBox(tr("좌측"), this);
    m_chkBubbleHNone  = new QCheckBox(tr("없음"), this);
    m_chkBubbleVNone  = new QCheckBox(tr("없음"), this);
    m_chkBubbleTop->setChecked(bubbleFlags.top);
    m_chkBubbleBottom->setChecked(bubbleFlags.bottom);
    m_chkBubbleLeft->setChecked(bubbleFlags.left);
    m_chkBubbleRight->setChecked(bubbleFlags.right);

    m_radioHX   = new QRadioButton(tr("X"), this);
    m_radioHY   = new QRadioButton(tr("Y"), this);
    m_radioHA   = new QRadioButton(tr("A"), this);
    m_radioHNum = new QRadioButton(tr("1"), this);
    m_bubbleHGroup = new QButtonGroup(this);
    m_bubbleHGroup->addButton(m_radioHX, 0);
    m_bubbleHGroup->addButton(m_radioHY, 1);
    m_bubbleHGroup->addButton(m_radioHA, 2);
    m_bubbleHGroup->addButton(m_radioHNum, 3);
    if (QAbstractButton *b = m_bubbleHGroup->button(bubbleFlags.hLabelType))
        b->setChecked(true);
    else
        m_radioHA->setChecked(true);

    m_radioVX   = new QRadioButton(tr("X"), this);
    m_radioVY   = new QRadioButton(tr("Y"), this);
    m_radioVA   = new QRadioButton(tr("A"), this);
    m_radioVNum = new QRadioButton(tr("1"), this);
    m_bubbleVGroup = new QButtonGroup(this);
    m_bubbleVGroup->addButton(m_radioVX, 0);
    m_bubbleVGroup->addButton(m_radioVY, 1);
    m_bubbleVGroup->addButton(m_radioVA, 2);
    m_bubbleVGroup->addButton(m_radioVNum, 3);
    if (QAbstractButton *b = m_bubbleVGroup->button(bubbleFlags.vLabelType))
        b->setChecked(true);
    else
        m_radioVA->setChecked(true);

    QHBoxLayout *bubbleHCheckRow = new QHBoxLayout;
    bubbleHCheckRow->addWidget(m_chkBubbleTop);
    bubbleHCheckRow->addWidget(m_chkBubbleBottom);
    bubbleHCheckRow->addWidget(m_chkBubbleHNone);

    QHBoxLayout *bubbleHRadioRow = new QHBoxLayout;
    bubbleHRadioRow->addWidget(m_radioHX);
    bubbleHRadioRow->addWidget(m_radioHY);
    bubbleHRadioRow->addWidget(m_radioHA);
    bubbleHRadioRow->addWidget(m_radioHNum);

    QVBoxLayout *bubbleHBoxLayout = new QVBoxLayout;
    bubbleHBoxLayout->addLayout(bubbleHCheckRow);
    bubbleHBoxLayout->addLayout(bubbleHRadioRow);

    QGroupBox *bubbleHBox = new QGroupBox(this);
    bubbleHBox->setLayout(bubbleHBoxLayout);

    QHBoxLayout *bubbleVCheckRow = new QHBoxLayout;
    bubbleVCheckRow->addWidget(m_chkBubbleRight);
    bubbleVCheckRow->addWidget(m_chkBubbleLeft);
    bubbleVCheckRow->addWidget(m_chkBubbleVNone);

    QHBoxLayout *bubbleVRadioRow = new QHBoxLayout;
    bubbleVRadioRow->addWidget(m_radioVX);
    bubbleVRadioRow->addWidget(m_radioVY);
    bubbleVRadioRow->addWidget(m_radioVA);
    bubbleVRadioRow->addWidget(m_radioVNum);

    QVBoxLayout *bubbleVBoxLayout = new QVBoxLayout;
    bubbleVBoxLayout->addLayout(bubbleVCheckRow);
    bubbleVBoxLayout->addLayout(bubbleVRadioRow);

    QGroupBox *bubbleVBox = new QGroupBox(this);
    bubbleVBox->setLayout(bubbleVBoxLayout);

    QVBoxLayout *bubbleLayout = new QVBoxLayout;
    bubbleLayout->addWidget(bubbleHBox);
    bubbleLayout->addWidget(bubbleVBox);

    QGroupBox *bubbleGroup = new QGroupBox(tr("주열번호"), this);
    bubbleGroup->setLayout(bubbleLayout);

    connect(m_chkBubbleHNone, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_chkBubbleTop->setChecked(false);
            m_chkBubbleBottom->setChecked(false);
        }
        m_chkBubbleTop->setEnabled(!checked);
        m_chkBubbleBottom->setEnabled(!checked);
    });

    connect(m_chkBubbleVNone, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) {
            m_chkBubbleRight->setChecked(false);
            m_chkBubbleLeft->setChecked(false);
        }
        m_chkBubbleRight->setEnabled(!checked);
        m_chkBubbleLeft->setEnabled(!checked);
    });

    QVBoxLayout *rightCol = new QVBoxLayout;
    rightCol->addWidget(group);
    rightCol->addWidget(m_btnSave);
    rightCol->addWidget(dimGroup);
    rightCol->addWidget(bubbleGroup);

    QVBoxLayout *fileListCol = new QVBoxLayout;
    fileListCol->addWidget(m_fileList);
    fileListCol->addWidget(m_btnDelete);

    QHBoxLayout *bodyRow = new QHBoxLayout;
    bodyRow->addLayout(fileListCol);
    bodyRow->addLayout(rightCol, 1);

    // ── 하단 버튼 ───────────────────────────────────────────────────────────────
    m_btnOk     = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_btnOk);
    btnRow->addWidget(m_btnCancel);

    // ── 메인 레이아웃 ──────────────────────────────────────────────────────────
    QVBoxLayout *main = new QVBoxLayout(this);
    main->addLayout(bodyRow);
    main->addSpacing(8);
    main->addLayout(btnRow);

    connect(m_btnOk,     &QPushButton::clicked, this, &GridCenterSetupDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked, this, &GridCenterSetupDialog::onCancelClicked);
    connect(m_btnSave,   &QPushButton::clicked, this, &GridCenterSetupDialog::onSaveClicked);
    connect(m_btnDelete, &QPushButton::clicked, this, &GridCenterSetupDialog::onDeleteClicked);
    connect(m_fileList,  &QListWidget::itemClicked, this, &GridCenterSetupDialog::onFileItemClicked);
    connect(m_editYSpacing, &QLineEdit::textChanged, this, &GridCenterSetupDialog::updateActiveHighlight);
    connect(m_editXSpacing, &QLineEdit::textChanged, this, &GridCenterSetupDialog::updateActiveHighlight);

    refreshFileList();
    updateActiveHighlight();
}

QString GridCenterSetupDialog::ySpacing() const { return m_editYSpacing->text().trimmed(); }
QString GridCenterSetupDialog::xSpacing() const { return m_editXSpacing->text().trimmed(); }

GridDimFlags GridCenterSetupDialog::dimFlags() const {
    GridDimFlags f;
    f.top    = m_chkTop->isChecked();
    f.bottom = m_chkBottom->isChecked();
    f.left   = m_chkLeft->isChecked();
    f.right  = m_chkRight->isChecked();
    f.total  = m_chkTotal->isChecked();
    return f;
}

GridBubbleFlags GridCenterSetupDialog::bubbleFlags() const {
    GridBubbleFlags f;
    f.top        = m_chkBubbleTop->isChecked();
    f.bottom     = m_chkBubbleBottom->isChecked();
    f.left       = m_chkBubbleLeft->isChecked();
    f.right      = m_chkBubbleRight->isChecked();
    f.hLabelType = m_bubbleHGroup->checkedId();
    f.vLabelType = m_bubbleVGroup->checkedId();
    return f;
}

void GridCenterSetupDialog::onOkClicked()     { accept(); }
void GridCenterSetupDialog::onCancelClicked() { reject(); }

void GridCenterSetupDialog::refreshFileList() {
    m_fileList->clear();
    QDir dir(resolveDataDir());
    const QStringList files = dir.entryList(QStringList() << "*.cen", QDir::Files, QDir::Name);
    for (const QString &fileName : files) {
        QString y, x;
        readCenFile(dir.filePath(fileName), y, x);
        QListWidgetItem *item = new QListWidgetItem(QFileInfo(fileName).completeBaseName());
        item->setData(YRole, y);
        item->setData(XRole, x);
        m_fileList->addItem(item);
    }
}

void GridCenterSetupDialog::onFileItemClicked(QListWidgetItem *item) {
    if (!item)
        return;
    QString filePath = QDir(resolveDataDir()).filePath(item->text() + ".cen");
    loadCenFile(filePath);
}

void GridCenterSetupDialog::loadCenFile(const QString &filePath) {
    QString y, x;
    if (!readCenFile(filePath, y, x))
        return;
    m_editYSpacing->setText(y);
    m_editXSpacing->setText(x);
}

// 현재 입력된 간격과 내용이 일치하는 파일을 목록에서 찾아 굵게 표시하고 선택한다
// (선택 상태는 포커스를 잃으면 흐려지므로, 굵은 글씨로 항상 눈에 띄게 유지).
void GridCenterSetupDialog::updateActiveHighlight() {
    const QString curY = m_editYSpacing->text().trimmed();
    const QString curX = m_editXSpacing->text().trimmed();

    QListWidgetItem *match = nullptr;
    for (int i = 0; i < m_fileList->count(); ++i) {
        QListWidgetItem *item = m_fileList->item(i);
        QFont f = item->font();
        bool isMatch = (item->data(YRole).toString() == curY &&
                        item->data(XRole).toString() == curX);
        f.setBold(isMatch);
        item->setFont(f);
        if (isMatch && !match)
            match = item;
    }

    m_fileList->setCurrentItem(match);
}

void GridCenterSetupDialog::onSaveClicked() {
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("중심선 간격 저장"),
                                         tr("파일 이름 :"), QLineEdit::Normal,
                                         QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;
    name = name.trimmed();

    QDir dir(resolveDataDir());
    if (!dir.exists())
        dir.mkpath(".");

    QString filePath = dir.filePath(name + ".cen");
    if (QFile::exists(filePath)) {
        auto ret = QMessageBox::question(this, tr("덮어쓰기 확인"),
                                          tr("'%1' 파일이 이미 존재합니다. 덮어쓸까요?").arg(name));
        if (ret != QMessageBox::Yes)
            return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("저장 실패"), tr("파일을 저장할 수 없습니다."));
        return;
    }

    QTextStream out(&file);
    out << "Y=" << m_editYSpacing->text().trimmed() << "\n";
    out << "X=" << m_editXSpacing->text().trimmed() << "\n";
    file.close();

    refreshFileList();
    updateActiveHighlight();
}

void GridCenterSetupDialog::onDeleteClicked() {
    QListWidgetItem *item = m_fileList->currentItem();
    if (!item) {
        QMessageBox::information(this, tr("파일 삭제"), tr("삭제할 파일을 목록에서 선택하세요."));
        return;
    }

    QString name = item->text();
    auto ret = QMessageBox::question(this, tr("파일 삭제 확인"),
                                      tr("'%1' 파일을 삭제할까요?").arg(name));
    if (ret != QMessageBox::Yes)
        return;

    QString filePath = QDir(resolveDataDir()).filePath(name + ".cen");
    if (!QFile::remove(filePath)) {
        QMessageBox::warning(this, tr("삭제 실패"), tr("파일을 삭제할 수 없습니다."));
        return;
    }

    refreshFileList();
    updateActiveHighlight();
}
