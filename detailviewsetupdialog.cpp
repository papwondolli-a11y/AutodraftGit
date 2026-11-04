#include "detailviewsetupdialog.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QPixmap>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

DetailViewSetupDialog::DetailViewSetupDialog(
    const QList<QPair<QString, QColor>> &layers, const QString &currentLayer,
    double labelTextSize, bool labelOnLeader, bool labelOnBoundary,
    const QString &scaleText, QWidget *parent)
    : QDialog(parent) {
    setWindowTitle(tr("상세도 설정"));
    setModal(true);

    // ── 레이어 (색상아이콘 + 이름) ──────────────────────────────────────────
    m_comboLayer = new QComboBox(this);
    for (int i = 0; i < layers.size(); ++i) {
        QPixmap pm(16, 16);
        pm.fill(layers.at(i).second);
        m_comboLayer->addItem(QIcon(pm), layers.at(i).first);
    }
    int li = m_comboLayer->findText(currentLayer);
    if (li >= 0)
        m_comboLayer->setCurrentIndex(li);

    // ── 라벨 문자 크기 ─────────────────────────────────────────────────────
    m_spinLabelSize = new QDoubleSpinBox(this);
    m_spinLabelSize->setRange(0.1, 100000.0);
    m_spinLabelSize->setDecimals(2);
    m_spinLabelSize->setSingleStep(0.5);
    m_spinLabelSize->setValue(labelTextSize > 0.0 ? labelTextSize : 2.5);

    // ── 라벨 문자 위치 (택1) ───────────────────────────────────────────────
    QWidget     *posWidget = new QWidget(this);
    QHBoxLayout *posRow    = new QHBoxLayout(posWidget);
    posRow->setContentsMargins(0, 0, 0, 0);
    m_radioLeader   = new QRadioButton(tr("지시선"), posWidget);
    m_radioBoundary = new QRadioButton(tr("경계선"), posWidget);
    // posWidget 내 QRadioButton 은 기본 auto-exclusive → 상호 배타
    if (labelOnBoundary && !labelOnLeader)
        m_radioBoundary->setChecked(true);
    else
        m_radioLeader->setChecked(true); // 기본
    posRow->addWidget(m_radioLeader);
    posRow->addWidget(m_radioBoundary);
    posRow->addStretch();

    // ── 상세도 축척 (편집 가능 콤보) ───────────────────────────────────────
    m_comboScale = new QComboBox(this);
    m_comboScale->setEditable(true);
    m_comboScale->addItems({QStringLiteral("2:1"), QStringLiteral("5:1"),
                            QStringLiteral("10:1"), QStringLiteral("1:2"),
                            QStringLiteral("1:5"), QStringLiteral("1:10")});
    m_comboScale->setCurrentText(scaleText.isEmpty() ? QStringLiteral("2:1")
                                                     : scaleText);

    QFormLayout *form = new QFormLayout;
    form->setLabelAlignment(Qt::AlignRight);
    form->addRow(tr("레이어 :"),        m_comboLayer);
    form->addRow(tr("라벨 문자 크기 :"), m_spinLabelSize);
    form->addRow(tr("라벨 문자 위치 :"), posWidget);
    form->addRow(tr("상세도 축척 :"),    m_comboScale);

    QGroupBox *grp = new QGroupBox(tr("상세도 설정"), this);
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
            &DetailViewSetupDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked, this,
            &DetailViewSetupDialog::onCancelClicked);
}

QString DetailViewSetupDialog::layerName() const {
    return m_comboLayer->currentText();
}
double DetailViewSetupDialog::labelTextSize() const {
    return m_spinLabelSize->value();
}
bool DetailViewSetupDialog::labelOnLeader() const {
    return m_radioLeader->isChecked();
}
bool DetailViewSetupDialog::labelOnBoundary() const {
    return m_radioBoundary->isChecked();
}
QString DetailViewSetupDialog::scaleText() const {
    return m_comboScale->currentText().trimmed();
}

void DetailViewSetupDialog::onOkClicked() { accept(); }
void DetailViewSetupDialog::onCancelClicked() { reject(); }
