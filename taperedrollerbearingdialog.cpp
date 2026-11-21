#include "taperedrollerbearingdialog.h"

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

TaperedRollerBearingDialog::TaperedRollerBearingDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("테이퍼롤러베어링"));
    setupUI();
}

void TaperedRollerBearingDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout;

    // 계열 콤보 (320, 302, 322, 303, 323) → 고르면 아래 호칭번호 목록이 그 계열로 바뀐다.
    m_family = new QComboBox(this);
    const QStringList families = TaperedRollerBearingTable::families();
    for (int i = 0; i < families.size(); ++i) m_family->addItem(families.at(i), families.at(i));
    form->addRow(tr("계열 :"), m_family);

    m_series = new QComboBox(this);
    form->addRow(tr("호칭번호 :"), m_series);

    m_d = makeSpin(this, 0.1);
    m_D = makeSpin(this, 0.1);
    m_B = makeSpin(this, 0.1);
    m_T = makeSpin(this, 0.1);
    m_C = makeSpin(this, 0.1);
    m_r = makeSpin(this, 0.0);
    m_R = makeSpin(this, 0.0);
    m_angle = new QDoubleSpinBox(this);
    m_angle->setDecimals(1);
    m_angle->setRange(5.0, 35.0);
    m_angle->setSuffix(QStringLiteral(" °"));
    m_angle->setToolTip(tr("롤러·레이스 경사각 (BE2.LSP 기본 15°)"));
    form->addRow(tr("콘 안지름 d :"), m_d);
    form->addRow(tr("컵 바깥지름 D :"), m_D);
    form->addRow(tr("콘 폭 B :"), m_B);
    form->addRow(tr("전체 폭 T :"), m_T);
    form->addRow(tr("컵 폭 C :"), m_C);
    form->addRow(tr("컵 후면 반경 r :"), m_r);
    form->addRow(tr("콘 후면 반경 R :"), m_R);
    form->addRow(tr("롤러 경사각 :"), m_angle);
    mainLayout->addLayout(form);

    m_dims   = new QCheckBox(tr("치수 기입 (T, C, B, D, d)"), this);
    m_center = new QCheckBox(tr("중심선 그리기 (베어링 중심선 + 롤러 축선)"), this);
    m_hatch  = new QCheckBox(tr("단면 해칭"), this);
    mainLayout->addWidget(m_dims);
    mainLayout->addWidget(m_center);
    m_hatchScale = new QDoubleSpinBox(this);
    m_hatchScale->setDecimals(2);
    m_hatchScale->setRange(0.01, 100.0);
    m_hatchScale->setSingleStep(0.05);
    m_hatchScale->setToolTip(tr("d, D 를 바꾸면 컵·콘 두께에 맞춰 자동 계산됩니다. 직접 수정할 수 있습니다."));
    QHBoxLayout *hatchLay = new QHBoxLayout;
    hatchLay->setContentsMargins(0, 0, 0, 0);
    hatchLay->addWidget(m_hatch);
    hatchLay->addWidget(new QLabel(tr("스케일 :"), this));
    hatchLay->addWidget(m_hatchScale);
    hatchLay->addStretch();
    mainLayout->addLayout(hatchLay);
    connect(m_hatch, &QCheckBox::toggled, m_hatchScale, &QWidget::setEnabled);
    m_full = new QCheckBox(tr("전체 그리기 (위·아래 연결선)"), this);
    m_full->setToolTip(tr("켜면 BE2.LSP 처럼 면선으로 위·아래 단면을 연결합니다.\n끄면 위·아래 단면만 그립니다."));
    mainLayout->addWidget(m_full);

    QLabel *hint = new QLabel(
        tr("기준점은 베어링 전체 폭(T)의 가운데, 축선 위입니다. 축선은 수평으로 그려집니다.\n"
           "r, R 값은 규격표에 없어 ISO 355 경향으로 채운 추정값입니다. 필요하면 직접 수정하세요."), this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &TaperedRollerBearingDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &TaperedRollerBearingDialog::reject);
    mainLayout->addWidget(buttons);

    connect(m_family, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TaperedRollerBearingDialog::onFamilyChanged);
    connect(m_series, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &TaperedRollerBearingDialog::onSeriesChanged);
    // d, D 가 바뀌면 해칭 스케일을 다시 계산한다.
    connect(m_d, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TaperedRollerBearingDialog::updateAutoHatchScale);
    connect(m_D, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &TaperedRollerBearingDialog::updateAutoHatchScale);

    // 처음 열 때: 첫 계열의 호칭번호 목록과 규격 값을 채운다 (setParams 가 저장값으로 덮어씀).
    onFamilyChanged(m_family->currentIndex());
}

void TaperedRollerBearingDialog::onFamilyChanged(int index) {
    if (index < 0) return;
    const QString family = m_family->itemData(index).toString();
    const QList<TaperedRollerBearingSpec> list = TaperedRollerBearingTable::byFamily(family);

    m_series->blockSignals(true);
    m_series->clear();
    for (int i = 0; i < list.size(); ++i) {
        const TaperedRollerBearingSpec &s = list.at(i);
        m_series->addItem(QStringLiteral("%1  (%2 x %3 x %4)")
                              .arg(s.name).arg(s.d).arg(s.D).arg(s.T),
                          s.name);
    }
    m_series->setCurrentIndex(list.isEmpty() ? -1 : 0);
    m_series->blockSignals(false);
    onSeriesChanged(m_series->currentIndex());
}

void TaperedRollerBearingDialog::onSeriesChanged(int index) {
    if (index < 0) return;
    const TaperedRollerBearingSpec s =
        TaperedRollerBearingTable::find(m_series->itemData(index).toString());
    if (s.name.isEmpty()) return;
    m_d->setValue(s.d);
    m_D->setValue(s.D);
    m_B->setValue(s.B);
    m_T->setValue(s.T);
    m_C->setValue(s.C);
    m_r->setValue(s.r);
    m_R->setValue(s.R);
}

void TaperedRollerBearingDialog::updateAutoHatchScale() {
    m_hatchScale->setValue(TaperedRollerBearingTable::autoHatchScale(m_d->value(), m_D->value()));
}

void TaperedRollerBearingDialog::setParams(const TaperedRollerBearingParams &params) {
    // 저장된 호칭번호의 계열을 먼저 고르고(목록 갱신), 그 안에서 호칭번호를 고른다.
    const int famIdx = m_family->findData(TaperedRollerBearingTable::familyOf(params.name));
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
    m_T->setValue(params.T);
    m_C->setValue(params.C);
    m_r->setValue(params.r);
    m_R->setValue(params.R);
    m_angle->setValue(params.angle);
    m_dims->setChecked(params.drawDims);
    m_center->setChecked(params.drawCenter);
    m_hatch->setChecked(params.drawHatch);
    // 저장된 스케일이 있으면 그 값, 없으면 d/D 에서 자동 계산한 값을 초기값으로 쓴다.
    if (params.hatchScale > 0.0) m_hatchScale->setValue(params.hatchScale);
    else                         updateAutoHatchScale();
    m_hatchScale->setEnabled(params.drawHatch);
    m_full->setChecked(params.drawFull);
}

TaperedRollerBearingParams TaperedRollerBearingDialog::params() const {
    TaperedRollerBearingParams p;
    p.name       = m_series->currentData().toString();
    p.d          = m_d->value();
    p.D          = m_D->value();
    p.B          = m_B->value();
    p.T          = m_T->value();
    p.C          = m_C->value();
    p.r          = m_r->value();
    p.R          = m_R->value();
    p.angle      = m_angle->value();
    p.drawDims   = m_dims->isChecked();
    p.drawCenter = m_center->isChecked();
    p.drawHatch  = m_hatch->isChecked();
    p.hatchScale = m_hatchScale->value();
    p.drawFull   = m_full->isChecked();
    return p;
}
