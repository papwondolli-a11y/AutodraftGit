#include "linetypeselectdialog.h"

#include "appcontext.h"

#include <QButtonGroup>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QListView>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QVBoxLayout>

LineTypeSelectDialog::LineTypeSelectDialog(AppContext *context,
                                          const QString &currentLinetype,
                                          QWidget *parent)
    : QDialog(parent), m_appContext(context), m_currentLinetype(currentLinetype) {
    setWindowTitle(tr("라인타입 선택"));

    m_listView = new QListView(this);
    m_model = new QStandardItemModel(this);
    m_listView->setModel(m_model);
    m_listView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_listView->setIconSize(QSize(48, 16)); // 모양 미리보기가 보이도록

    // 그룹 라디오 (OK 버튼 위) — LineTypeManager::currentGroup() 과 연동
    m_rbAutoDraft = new QRadioButton(tr("AutoDraft"), this);
    m_rbAcadStd = new QRadioButton(tr("AutoCAD호환(S)"), this);
    m_rbAcadIso = new QRadioButton(tr("AutoCAD호환(ISO)"), this);

    const LineTypeManager::LinetypeGroup g = LineTypeManager::currentGroup();
    m_rbAcadStd->setChecked(g == LineTypeManager::LinetypeGroup::AcadStandard);
    m_rbAcadIso->setChecked(g == LineTypeManager::LinetypeGroup::AcadIso);
    m_rbAutoDraft->setChecked(!m_rbAcadStd->isChecked() && !m_rbAcadIso->isChecked());

    QButtonGroup *radioGroup = new QButtonGroup(this);
    radioGroup->addButton(m_rbAutoDraft);
    radioGroup->addButton(m_rbAcadStd);
    radioGroup->addButton(m_rbAcadIso);
    connect(m_rbAutoDraft, &QRadioButton::toggled, this, &LineTypeSelectDialog::onGroupChanged);
    connect(m_rbAcadStd, &QRadioButton::toggled, this, &LineTypeSelectDialog::onGroupChanged);
    connect(m_rbAcadIso, &QRadioButton::toggled, this, &LineTypeSelectDialog::onGroupChanged);

    QHBoxLayout *radioLayout = new QHBoxLayout();
    radioLayout->addWidget(m_rbAutoDraft);
    radioLayout->addWidget(m_rbAcadStd);
    radioLayout->addWidget(m_rbAcadIso);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->addWidget(m_listView);
    layout->addLayout(radioLayout);
    layout->addWidget(buttons);

    reloadList();
}

LineTypeManager::LinetypeGroup LineTypeSelectDialog::currentRadioGroup() const {
    if (m_rbAcadStd->isChecked())
        return LineTypeManager::LinetypeGroup::AcadStandard;
    if (m_rbAcadIso->isChecked())
        return LineTypeManager::LinetypeGroup::AcadIso;
    return LineTypeManager::LinetypeGroup::AutoDraft;
}

void LineTypeSelectDialog::onGroupChanged() {
    // toggled 는 해제/선택 두 번 오지만 재구성이 가벼워 그대로 둔다.
    LineTypeManager::setCurrentGroup(currentRadioGroup()); // 새 레이어 만들기와 공유·유지
    reloadList();
}

void LineTypeSelectDialog::reloadList() {
    m_model->clear();
    if (!m_appContext || !m_appContext->lineTypeManager())
        return;
    LineTypeManager *ltm = m_appContext->lineTypeManager();

    QList<LineType> types = ltm->getLinetypesInGroup(currentRadioGroup());

    // "Continuous"(가는 실선)는 linetype.lin 에 없는 특수 항목 — 어느 그룹이든 맨 위에 항상 둔다.
    // (새 레이어 만들기 콤보와 동일. 패턴이 비어 있으므로 previewIconFor 가 실선 아이콘을 그린다.)
    types.prepend(LineType("Continuous", QString()));

    // 편집 대상 레이어의 기존 선종류가 이 그룹에 없으면 맨 앞(Continuous 다음)에 넣어 선택 가능하게.
    bool curInList = false;
    for (int i = 0; i < types.size(); ++i) {
        if (types.at(i).name.compare(m_currentLinetype, Qt::CaseInsensitive) == 0) {
            curInList = true;
            break;
        }
    }
    if (!curInList) {
        const LineType cur = ltm->getLinetype(m_currentLinetype);
        if (!cur.name.isEmpty())
            types.insert(1, cur); // Continuous 바로 아래
    }

    // 레이어 선종류가 비었거나 continuous 계열이면 "Continuous"(row 0)를 선택 대상으로 본다.
    const QString curLow = m_currentLinetype.trimmed().toLower();
    const bool curIsContinuous =
        curLow.isEmpty() || curLow == "continuous" || curLow == "continous" ||
        curLow == "bylayer" || curLow == "byblock";

    int selectRow = -1;
    for (int i = 0; i < types.size(); ++i) {
        const LineType &lt = types.at(i);
        QStandardItem *item = new QStandardItem(ltm->previewIconFor(lt.name), lt.name);
        item->setEditable(false);
        m_model->appendRow(item);
        if (selectRow < 0 && lt.name.compare(m_currentLinetype, Qt::CaseInsensitive) == 0)
            selectRow = i;
    }
    if (selectRow < 0 && curIsContinuous)
        selectRow = 0; // "Continuous" 행
    if (selectRow >= 0)
        m_listView->setCurrentIndex(m_model->index(selectRow, 0));
}

QString LineTypeSelectDialog::selectedLinetype() const {
    const QModelIndex idx = m_listView->currentIndex();
    if (!idx.isValid())
        return QString();
    return idx.data(Qt::DisplayRole).toString();
}
