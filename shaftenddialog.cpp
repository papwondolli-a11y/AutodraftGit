#include "shaftenddialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QVBoxLayout>

namespace {
QDoubleSpinBox *makeSpin(QWidget *parent, double minValue, double maxValue = 100000.0) {
    QDoubleSpinBox *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(3);
    spin->setRange(minValue, maxValue);
    spin->setSuffix(QStringLiteral(" mm"));
    return spin;
}
}

ShaftEndDialog::ShaftEndDialog(ShaftEndType type, double segDiameter, double segLength,
                               QWidget *parent)
    : QDialog(parent), m_type(type), m_segDiameter(segDiameter), m_segLength(segLength)
{
    if (m_type == ShaftEndType::Keyway) {
        setWindowTitle(tr("키홈 크기"));
        setupKeywayUI();
    } else {
        setWindowTitle(tr("나사부"));
        setupThreadUI();
    }
}

void ShaftEndDialog::setupKeywayUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout;

    m_kwLength = makeSpin(this, 0.001);
    m_kwUpper  = makeSpin(this, 0.001);
    m_kwCenter = makeSpin(this, -100000.0);
    m_kwLower  = makeSpin(this, 0.001);
    form->addRow(tr("길이 (끝면에서 홈 안쪽 끝까지) :"), m_kwLength);
    form->addRow(tr("상단 (홈 중앙선에서 위쪽 벽까지) :"), m_kwUpper);
    form->addRow(tr("중앙 (축 중심선에서 홈 중앙선까지, 위쪽 +) :"), m_kwCenter);
    form->addRow(tr("하단 (홈 중앙선에서 아래쪽 벽까지) :"), m_kwLower);
    m_kwSection = new QCheckBox(tr("단면으로 표시 (위쪽 절반을 해칭 단면으로)"), this);
    m_kwDepth = makeSpin(this, 0.001);
    form->addRow(m_kwSection);
    form->addRow(tr("단면 홈 깊이 (축 윗면에서 홈 바닥까지) :"), m_kwDepth);
    m_kwPos = new QComboBox(this);
    m_kwPos->addItems({tr("앞 (구간 앞쪽)"), tr("중앙 (구간 중앙)"), tr("뒤 (구간 뒤쪽)")});
    form->addRow(tr("단면 위치 :"), m_kwPos);
    m_kwDepth->setEnabled(false);
    m_kwPos->setEnabled(false);
    connect(m_kwSection, &QCheckBox::toggled, m_kwDepth, &QWidget::setEnabled);
    connect(m_kwSection, &QCheckBox::toggled, m_kwPos, &QWidget::setEnabled);
    mainLayout->addLayout(form);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    mainLayout->addWidget(m_info);
    connect(m_kwLength, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShaftEndDialog::updateInfo);
    connect(m_kwUpper,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShaftEndDialog::updateInfo);
    connect(m_kwCenter, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShaftEndDialog::updateInfo);
    connect(m_kwLower,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShaftEndDialog::updateInfo);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

void ShaftEndDialog::setupThreadUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout;

    m_thLength = makeSpin(this, 0.001);
    m_thPitch  = makeSpin(this, 0.0, 100.0);
    form->addRow(tr("나사 길이 (끝면에서 나사 시작선까지) :"), m_thLength);
    form->addRow(tr("피치 (0 = 표준 거친나사 자동) :"), m_thPitch);
    mainLayout->addLayout(form);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    mainLayout->addWidget(m_info);
    connect(m_thLength, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShaftEndDialog::updateInfo);
    connect(m_thPitch,  QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShaftEndDialog::updateInfo);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

void ShaftEndDialog::setKeyway(const ShaftKeyway &k) {
    m_kwLength->setValue(k.length);
    m_kwUpper->setValue(k.upper);
    m_kwCenter->setValue(k.center);
    m_kwLower->setValue(k.lower);
    m_kwDepth->setValue(k.sectionDepth);
    m_kwPos->setCurrentIndex(qBound(0, k.sectionPos < 0 ? 2 : k.sectionPos, 2));
    m_kwSection->setChecked(k.showSection);
    updateInfo();
}

ShaftKeyway ShaftEndDialog::keyway() const {
    ShaftKeyway k;
    k.length = m_kwLength->value();
    k.upper  = m_kwUpper->value();
    k.center = m_kwCenter->value();
    k.lower  = m_kwLower->value();
    k.showSection  = m_kwSection->isChecked();
    k.sectionDepth = m_kwDepth->value();
    k.sectionPos   = m_kwPos->currentIndex();
    return k;
}

void ShaftEndDialog::setThread(const ShaftThread &t) {
    m_thLength->setValue(t.length);
    m_thPitch->setValue(t.pitch);
    updateInfo();
}

ShaftThread ShaftEndDialog::thread() const {
    ShaftThread t;
    t.length = m_thLength->value();
    t.pitch  = m_thPitch->value();
    return t;
}

void ShaftEndDialog::updateInfo() {
    if (!m_info) return;
    if (m_type == ShaftEndType::Keyway) {
        m_info->setText(tr("홈 폭 = %1 mm · 끝 구간 지름 %2 mm, 길이 %3 mm\n"
                           "안쪽 끝은 반원이며 끝면 쪽은 열려 있습니다.")
                            .arg(m_kwUpper->value() + m_kwLower->value())
                            .arg(m_segDiameter).arg(m_segLength));
    } else {
        ShaftThread t = thread();
        const double pitch = ShaftParams::effectivePitch(t, m_segDiameter);
        m_info->setText(tr("호칭경 M%1 · 피치 %2 · 골지름 %3 mm\n"
                           "끝 구간 길이 %4 mm (나사 길이가 이보다 짧으면 부분나사로 그립니다).")
                            .arg(m_segDiameter).arg(pitch)
                            .arg(m_segDiameter - 1.082532 * pitch)
                            .arg(m_segLength));
    }
}
