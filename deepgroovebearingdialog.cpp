#include "deepgroovebearingdialog.h"

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

DeepGrooveBearingDialog::DeepGrooveBearingDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("깊은 홈 볼베어링"));
    setupUI();
}

void DeepGrooveBearingDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout;

    // 계열 콤보 (60, 62, 63, 64, 68, 69) → 고르면 아래 호칭번호 목록이 그 계열로 바뀐다.
    m_family = new QComboBox(this);
    const QStringList families = DeepGrooveBearingTable::families();
    for (int i = 0; i < families.size(); ++i) m_family->addItem(families.at(i), families.at(i));
    form->addRow(tr("계열 :"), m_family);

    m_series = new QComboBox(this);
    form->addRow(tr("호칭번호 :"), m_series);

    m_d = makeSpin(this, 0.1);
    m_D = makeSpin(this, 0.1);
    m_B = makeSpin(this, 0.1);
    m_r = makeSpin(this, 0.0);
    form->addRow(tr("안지름 d :"), m_d);
    form->addRow(tr("바깥지름 D :"), m_D);
    form->addRow(tr("폭 B :"), m_B);
    form->addRow(tr("모서리 둥글기 r :"), m_r);
    mainLayout->addLayout(form);

    m_dims   = new QCheckBox(tr("치수 기입 (B, ød, øD)"), this);
    m_center = new QCheckBox(tr("중심선 그리기"), this);
    m_hatch  = new QCheckBox(tr("단면 해칭"), this);
    mainLayout->addWidget(m_dims);
    mainLayout->addWidget(m_center);
    m_hatchScale = new QDoubleSpinBox(this);
    m_hatchScale->setDecimals(2);
    m_hatchScale->setRange(0.01, 100.0);
    m_hatchScale->setSingleStep(0.05);
    m_hatchScale->setToolTip(tr("d, D 를 바꾸면 링 높이에 맞춰 자동 계산됩니다. 직접 수정할 수 있습니다."));
    QHBoxLayout *hatchLay = new QHBoxLayout;
    hatchLay->setContentsMargins(0, 0, 0, 0);
    hatchLay->addWidget(m_hatch);
    hatchLay->addWidget(new QLabel(tr("스케일 :"), this));
    hatchLay->addWidget(m_hatchScale);
    hatchLay->addStretch();
    mainLayout->addLayout(hatchLay);
    m_full = new QCheckBox(tr("전체 그리기 (위·아래 연결선)"), this);
    m_full->setToolTip(tr("켜면 BE1.LSP 처럼 좌우 면 선으로 위·아래 단면을 연결합니다.\n끄면 위·아래 단면만 그립니다."));
    mainLayout->addWidget(m_full);
    connect(m_hatch, &QCheckBox::toggled, m_hatchScale, &QWidget::setEnabled);

    QLabel *hint = new QLabel(
        tr("기준점은 베어링 중심(축선 위, 폭의 가운데)입니다. 축선은 수평으로 그려집니다.\n"
           "표의 값을 고르면 d/D/B/r 이 채워지며 직접 수정할 수도 있습니다."), this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &DeepGrooveBearingDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &DeepGrooveBearingDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_family, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeepGrooveBearingDialog::onFamilyChanged);
    connect(m_series, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &DeepGrooveBearingDialog::onSeriesChanged);
    // d, D 가 바뀌면 해칭 스케일을 다시 계산한다.
    connect(m_d, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &DeepGrooveBearingDialog::updateAutoHatchScale);
    connect(m_D, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &DeepGrooveBearingDialog::updateAutoHatchScale);

    // 처음 열 때: 첫 계열(60)의 호칭번호 목록과 규격 값을 채운다 (setParams 가 저장값으로 덮어씀).
    onFamilyChanged(m_family->currentIndex());
}

void DeepGrooveBearingDialog::onFamilyChanged(int index) {
    if (index < 0) return;
    const QString family = m_family->itemData(index).toString();
    const QList<DeepGrooveBearingSpec> list = DeepGrooveBearingTable::byFamily(family);

    m_series->blockSignals(true);
    m_series->clear();
    for (int i = 0; i < list.size(); ++i) {
        const DeepGrooveBearingSpec &s = list.at(i);
        m_series->addItem(QStringLiteral("%1  (%2 x %3 x %4)")
                              .arg(s.name).arg(s.d).arg(s.D).arg(s.B),
                          s.name);
    }
    m_series->setCurrentIndex(list.isEmpty() ? -1 : 0);
    m_series->blockSignals(false);
    // 첫 호칭번호의 규격으로 d/D/B/r 을 채운다.
    onSeriesChanged(m_series->currentIndex());
}

void DeepGrooveBearingDialog::updateAutoHatchScale() {
    m_hatchScale->setValue(DeepGrooveBearingTable::autoHatchScale(m_d->value(), m_D->value()));
}

void DeepGrooveBearingDialog::onSeriesChanged(int index) {
    if (index < 0) return;
    const DeepGrooveBearingSpec s = DeepGrooveBearingTable::find(m_series->itemData(index).toString());
    if (s.name.isEmpty()) return;
    m_d->setValue(s.d);
    m_D->setValue(s.D);
    m_B->setValue(s.B);
    m_r->setValue(s.r);
}

void DeepGrooveBearingDialog::setParams(const DeepGrooveBearingParams &params) {
    // 저장된 호칭번호의 계열을 먼저 고르고(목록 갱신), 그 안에서 호칭번호를 고른다.
    const int famIdx = m_family->findData(DeepGrooveBearingTable::familyOf(params.name));
    if (famIdx >= 0) {
        if (m_family->currentIndex() == famIdx) onFamilyChanged(famIdx);
        else                                    m_family->setCurrentIndex(famIdx);
    }
    const int idx = m_series->findData(params.name);
    if (idx >= 0) m_series->setCurrentIndex(idx);
    // 저장된 값(직접 수정한 값 포함)으로 덮어쓴다.
    m_d->setValue(params.d);
    m_D->setValue(params.D);
    m_B->setValue(params.B);
    m_r->setValue(params.r);
    m_dims->setChecked(params.drawDims);
    m_center->setChecked(params.drawCenter);
    m_hatch->setChecked(params.drawHatch);
    // 저장된 스케일이 있으면 그 값, 없으면 d/D 에서 자동 계산한 값을 초기값으로 쓴다.
    if (params.hatchScale > 0.0) m_hatchScale->setValue(params.hatchScale);
    else                         updateAutoHatchScale();
    m_hatchScale->setEnabled(params.drawHatch);
    m_full->setChecked(params.drawFull);
}

DeepGrooveBearingParams DeepGrooveBearingDialog::params() const {
    DeepGrooveBearingParams p;
    p.name       = m_series->currentData().toString();
    p.d          = m_d->value();
    p.D          = m_D->value();
    p.B          = m_B->value();
    p.r          = m_r->value();
    p.drawDims   = m_dims->isChecked();
    p.drawCenter = m_center->isChecked();
    p.drawHatch  = m_hatch->isChecked();
    p.hatchScale = m_hatchScale->value();
    p.drawFull   = m_full->isChecked();
    return p;
}
