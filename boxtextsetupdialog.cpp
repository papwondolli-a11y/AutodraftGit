#include "boxtextsetupdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

BoxTextSetupDialog::BoxTextSetupDialog(
    const QList<QPair<QString, QColor>> &layers, const QString &boxLayer,
    const QString &textLayer, bool applyTextLayerToPicked, bool linkBoxSize,
    QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("박스글자 설정"));
    setModal(true);

    // ── 레이어 (색상아이콘 + 이름) ──────────────────────────────────────────
    m_comboBoxLayer  = createLayerCombo(layers, boxLayer);
    m_comboTextLayer = createLayerCombo(layers, textLayer);

    // ── 기존 글자 레이어 적용 (Pick 모드) ──────────────────────────────────
    m_chkApplyTextLayer = new QCheckBox(tr("기존 글자 레이어 적용 (Pick 모드에서 선택한 글자를 글자 레이어로 변경)"), this);
    m_chkApplyTextLayer->setChecked(applyTextLayerToPicked);

    // ── 박스크기 연동 ──────────────────────────────────────────────────────
    m_chkLinkBoxSize = new QCheckBox(tr("박스크기 연동 (글자 수정 시 박스 크기 자동 변경)"), this);
    m_chkLinkBoxSize->setChecked(linkBoxSize);

    QFormLayout *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(tr("박스 레이어 :"), m_comboBoxLayer);
    form->addRow(tr("글자 레이어 :"), m_comboTextLayer);
    form->addRow(QString(), m_chkApplyTextLayer);
    form->addRow(QString(), m_chkLinkBoxSize);

    QGroupBox *grp = new QGroupBox(tr("박스글자 설정"), this);
    grp->setLayout(form);

    // ── 버튼 ──────────────────────────────────────────────────────────────
    m_btnOk     = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);

    QHBoxLayout *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(m_btnOk);
    btnRow->addWidget(m_btnCancel);

    QVBoxLayout *main = new QVBoxLayout(this);
    main->addWidget(grp);
    main->addSpacing(6);
    main->addLayout(btnRow);

    connect(m_btnOk, &QPushButton::clicked, this,
            &BoxTextSetupDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked, this,
            &BoxTextSetupDialog::onCancelClicked);
}

QComboBox *BoxTextSetupDialog::createLayerCombo(
    const QList<QPair<QString, QColor>> &layers, const QString &current) {
    QComboBox *combo = new QComboBox(this);
    for (int i = 0; i < layers.size(); ++i) {
        QPixmap pm(16, 16);
        pm.fill(layers.at(i).second);
        combo->addItem(QIcon(pm), layers.at(i).first);
    }
    int idx = combo->findText(current);
    if (idx >= 0)
        combo->setCurrentIndex(idx);
    return combo;
}

QString BoxTextSetupDialog::boxLayer() const {
    return m_comboBoxLayer->currentText();
}
QString BoxTextSetupDialog::textLayer() const {
    return m_comboTextLayer->currentText();
}
bool BoxTextSetupDialog::applyTextLayerToPicked() const {
    return m_chkApplyTextLayer->isChecked();
}
bool BoxTextSetupDialog::linkBoxSize() const {
    return m_chkLinkBoxSize->isChecked();
}

void BoxTextSetupDialog::onOkClicked() { accept(); }
void BoxTextSetupDialog::onCancelClicked() { reject(); }
