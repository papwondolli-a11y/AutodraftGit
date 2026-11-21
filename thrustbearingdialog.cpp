#include "thrustbearingdialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {
QDoubleSpinBox *makeSpin(QWidget *parent, double minValue) {
    QDoubleSpinBox *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(3);
    spin->setRange(minValue, 100000.0);
    spin->setSuffix(QStringLiteral(" mm"));
    return spin;
}
}

ThrustBearingDialog::ThrustBearingDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("스러스트베어링"));
    setupUI();
}

void ThrustBearingDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout;

    m_mode = new QComboBox(this);
    m_mode->addItem(tr("단식 (한 방향, 511~514 계열)"), 0);
    m_mode->addItem(tr("복식 (양 방향, 522~524 계열)"), 1);
    form->addRow(tr("방식 :"), m_mode);

    m_type = new QComboBox(this);
    for (int t = 0; t < 3; ++t) m_type->addItem(ThrustBearingTable::typeLabel(t), t);
    form->addRow(tr("종류 :"), m_type);

    m_family = new QComboBox(this);
    const QStringList families = ThrustBearingTable::families();
    for (int i = 0; i < families.size(); ++i) m_family->addItem(families.at(i), families.at(i));
    form->addRow(tr("계열 :"), m_family);

    m_series = new QComboBox(this);
    form->addRow(tr("호칭번호 :"), m_series);

    m_d  = makeSpin(this, 0.1);
    m_de = makeSpin(this, 0.1);
    m_D  = makeSpin(this, 0.1);
    m_H  = makeSpin(this, 0.1);
    m_c  = makeSpin(this, 0.0);
    m_di = makeSpin(this, 0.1);
    m_a  = makeSpin(this, 0.1);
    m_c1 = makeSpin(this, 0.0);
    m_H1 = makeSpin(this, 0.1);
    m_H2 = makeSpin(this, 0.1);
    m_H1->setToolTip(tr("조심자리형 전체 높이 (KS 표 값이 없어 H 의 약 1.08배로 추정, 수정 가능)"));
    m_H2->setToolTip(tr("조심자리와셔형 전체 높이 (KS 표 값이 없어 H 의 약 1.2배로 추정, 수정 가능)"));
    m_diLabel = new QLabel(tr("가운데 와셔 안지름 di (복식) :"), this);
    m_aLabel  = new QLabel(tr("가운데 와셔 두께 a (복식) :"), this);
    m_c1Label = new QLabel(tr("가운데 와셔 모서리 반경 c1 (복식) :"), this);
    m_deLabel = new QLabel(tr("하우징 와셔 안지름 de :"), this);
    form->addRow(tr("축 와셔 안지름 d :"), m_d);
    form->addRow(m_deLabel, m_de);
    form->addRow(m_diLabel, m_di);
    form->addRow(m_aLabel, m_a);
    form->addRow(m_c1Label, m_c1);
    form->addRow(tr("바깥지름 D :"), m_D);
    form->addRow(tr("높이 H (단식: 평면자리형 / 복식: 평면 전체) :"), m_H);
    form->addRow(tr("모서리 반경 c :"), m_c);
    form->addRow(tr("높이 H1 (조심자리형) :"), m_H1);
    form->addRow(tr("높이 H2 (조심자리와셔형) :"), m_H2);
    mainLayout->addLayout(form);

    m_dims   = new QCheckBox(tr("치수 기입 (d, de, D, 높이)"), this);
    m_center = new QCheckBox(tr("중심선 그리기 (축선 + 볼 중심선)"), this);
    m_hatch  = new QCheckBox(tr("단면 해칭"), this);
    mainLayout->addWidget(m_dims);
    mainLayout->addWidget(m_center);
    m_hatchScale = new QDoubleSpinBox(this);
    m_hatchScale->setDecimals(2);
    m_hatchScale->setRange(0.01, 100.0);
    m_hatchScale->setSingleStep(0.05);
    m_hatchScale->setToolTip(tr("H 를 바꾸면 와셔 두께에 맞춰 자동 계산됩니다. 직접 수정할 수 있습니다."));
    QHBoxLayout *hatchLay = new QHBoxLayout;
    hatchLay->setContentsMargins(0, 0, 0, 0);
    hatchLay->addWidget(m_hatch);
    hatchLay->addWidget(new QLabel(tr("스케일 :"), this));
    hatchLay->addWidget(m_hatchScale);
    hatchLay->addStretch();
    mainLayout->addLayout(hatchLay);
    connect(m_hatch, &QCheckBox::toggled, m_hatchScale, &QWidget::setEnabled);
    m_full = new QCheckBox(tr("전체 그리기 (구멍 건너 연결선)"), this);
    m_full->setToolTip(tr("켜면 BE3.LSP 처럼 축 구멍 위를 가로지르는 면선을 함께 그립니다.\n끄면 좌우 단면만 그립니다."));
    mainLayout->addWidget(m_full);

    QLabel *hint = new QLabel(
        tr("기준점은 베어링 전체 높이의 가운데, 축선 위입니다. 축선은 수직으로 그려집니다.\n"
           "조심자리형(53xxx)은 512·513·514 계열에만 있습니다.\n"
           "모서리 반경 c, 높이 H1·H2 는 KS 표 값이 없어 추정한 값입니다. 필요하면 직접 수정하세요."), this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ThrustBearingDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ThrustBearingDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ThrustBearingDialog::onModeChanged);
    connect(m_type, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ThrustBearingDialog::onTypeChanged);
    connect(m_a, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ThrustBearingDialog::updateAutoHatchScale);
    connect(m_family, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ThrustBearingDialog::onFamilyChanged);
    connect(m_series, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ThrustBearingDialog::onSeriesChanged);
    connect(m_H, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ThrustBearingDialog::onHChanged);

    onFamilyChanged(m_family->currentIndex());
    updateTypeDependentUi();
}

bool ThrustBearingDialog::isDouble() const {
    return m_mode->currentData().toInt() == 1;
}

void ThrustBearingDialog::repopulateFamilies(bool dbl) {
    m_family->blockSignals(true);
    m_family->clear();
    const QStringList families = ThrustBearingTable::families(dbl);
    for (int i = 0; i < families.size(); ++i) m_family->addItem(families.at(i), families.at(i));
    m_family->blockSignals(false);
}

void ThrustBearingDialog::onModeChanged(int) {
    repopulateFamilies(isDouble());
    updateTypeDependentUi();
    onFamilyChanged(m_family->currentIndex());
}

void ThrustBearingDialog::updateTypeDependentUi() {
    const bool dbl = isDouble();
    m_de->setEnabled(!dbl);
    m_deLabel->setEnabled(!dbl);
    m_di->setEnabled(dbl);
    m_a->setEnabled(dbl);
    m_c1->setEnabled(dbl);
    m_diLabel->setEnabled(dbl);
    m_aLabel->setEnabled(dbl);
    m_c1Label->setEnabled(dbl);
    const int type = m_type->currentData().toInt();
    m_H1->setEnabled(type == 1 || type == 2);
    m_H2->setEnabled(type == 2);
}

void ThrustBearingDialog::refreshSeriesLabels() {
    const int type = m_type->currentData().toInt();
    for (int i = 0; i < m_series->count(); ++i) {
        const QString base = m_series->itemData(i).toString();
        const ThrustBearingSpec s = ThrustBearingTable::find(base);
        m_series->setItemText(i, QStringLiteral("%1  (%2 x %3 x %4)")
                                     .arg(ThrustBearingTable::displayName(base, type))
                                     .arg(s.d).arg(s.D).arg(s.H));
    }
}

void ThrustBearingDialog::onTypeChanged(int) {
    updateTypeDependentUi();
    refreshSeriesLabels();
    // 조심자리형은 511 계열에 없다 → 512 로 옮긴다.
    const int type = m_type->currentData().toInt();
    if (type != 0 && !ThrustBearingTable::hasSelfAligning(m_family->currentData().toString())) {
        const int idx = m_family->findData(QStringLiteral("512"));
        if (idx >= 0) m_family->setCurrentIndex(idx);
    }
}

void ThrustBearingDialog::onFamilyChanged(int index) {
    if (index < 0) return;
    const QString family = m_family->itemData(index).toString();
    // 511 계열은 평면자리형만 있다.
    if (!ThrustBearingTable::hasSelfAligning(family) && m_type->currentData().toInt() != 0) {
        m_type->blockSignals(true);
        m_type->setCurrentIndex(0);
        m_type->blockSignals(false);
        updateTypeDependentUi();
    }
    const QList<ThrustBearingSpec> list = ThrustBearingTable::byFamily(family);

    m_series->blockSignals(true);
    m_series->clear();
    for (int i = 0; i < list.size(); ++i) m_series->addItem(list.at(i).name, list.at(i).name);
    refreshSeriesLabels();
    m_series->setCurrentIndex(list.isEmpty() ? -1 : 0);
    m_series->blockSignals(false);
    onSeriesChanged(m_series->currentIndex());
}

void ThrustBearingDialog::onSeriesChanged(int index) {
    if (index < 0) return;
    const ThrustBearingSpec s = ThrustBearingTable::find(m_series->itemData(index).toString());
    if (s.name.isEmpty()) return;
    m_d->setValue(s.d);
    m_de->setValue(s.de);
    m_D->setValue(s.D);
    m_c->setValue(s.c);
    if (s.dbl) {
        m_di->setValue(s.di);
        m_a->setValue(s.a);
        m_c1->setValue(s.c1);
    }
    m_H->setValue(s.H);   // onHChanged 가 H1, H2, 해칭 스케일을 갱신
    onHChanged();
}

void ThrustBearingDialog::onHChanged() {
    m_H1->setValue(ThrustBearingTable::estimateH1(m_H->value(), isDouble()));
    m_H2->setValue(ThrustBearingTable::estimateH2(m_H->value(), isDouble()));
    updateAutoHatchScale();
}

void ThrustBearingDialog::updateAutoHatchScale() {
    m_hatchScale->setValue(ThrustBearingTable::autoHatchScale(m_H->value(), isDouble() ? m_a->value() : -1.0));
}

void ThrustBearingDialog::setParams(const ThrustBearingParams &params) {
    const int modeIdx = m_mode->findData(params.dbl ? 1 : 0);
    if (modeIdx >= 0 && modeIdx != m_mode->currentIndex()) m_mode->setCurrentIndex(modeIdx);
    const int typeIdx = m_type->findData(params.type);
    if (typeIdx >= 0) m_type->setCurrentIndex(typeIdx);
    const int famIdx = m_family->findData(ThrustBearingTable::familyOf(params.name));
    if (famIdx >= 0) {
        if (m_family->currentIndex() == famIdx) onFamilyChanged(famIdx);
        else                                    m_family->setCurrentIndex(famIdx);
    }
    const int idx = m_series->findData(params.name);
    if (idx >= 0) m_series->setCurrentIndex(idx);
    // 저장된 값(직접 수정한 값 포함)으로 덮어쓴다.
    m_d->setValue(params.d);
    m_de->setValue(params.de);
    m_D->setValue(params.D);
    m_c->setValue(params.c);
    m_di->setValue(params.di);
    m_a->setValue(params.a);
    m_c1->setValue(params.c1);
    m_H->setValue(params.H);
    m_H1->setValue(params.H1);
    m_H2->setValue(params.H2);
    m_dims->setChecked(params.drawDims);
    m_center->setChecked(params.drawCenter);
    m_hatch->setChecked(params.drawHatch);
    if (params.hatchScale > 0.0) m_hatchScale->setValue(params.hatchScale);
    else                         updateAutoHatchScale();
    m_hatchScale->setEnabled(params.drawHatch);
    m_full->setChecked(params.drawFull);
    updateTypeDependentUi();
}

ThrustBearingParams ThrustBearingDialog::params() const {
    ThrustBearingParams p;
    p.name       = m_series->currentData().toString();
    p.type       = m_type->currentData().toInt();
    p.d          = m_d->value();
    p.de         = m_de->value();
    p.D          = m_D->value();
    p.H          = m_H->value();
    p.c          = m_c->value();
    p.dbl        = isDouble();
    p.di         = m_di->value();
    p.a          = m_a->value();
    p.c1         = m_c1->value();
    p.H1         = m_H1->value();
    p.H2         = m_H2->value();
    p.drawDims   = m_dims->isChecked();
    p.drawCenter = m_center->isChecked();
    p.drawHatch  = m_hatch->isChecked();
    p.hatchScale = m_hatchScale->value();
    p.drawFull   = m_full->isChecked();
    return p;
}
