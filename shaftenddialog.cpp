#include "shaftenddialog.h"

#include "shaftbuilder.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStandardItemModel>
#include <QVBoxLayout>
#include <QtMath>

namespace {
QDoubleSpinBox *makeSpin(QWidget *parent, double minValue, double maxValue = 100000.0) {
    QDoubleSpinBox *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(3);
    spin->setRange(minValue, maxValue);
    spin->setSuffix(QStringLiteral(" mm"));
    return spin;
}

QString num(double v) {
    return QString::number(v, 'g', 6);
}
} // namespace

// ═════════════════════════════════════════════════════════════════════════
// ShaftKeywayDialog
// ═════════════════════════════════════════════════════════════════════════
ShaftKeywayDialog::ShaftKeywayDialog(int index, int segCount, double segDiameter, double segLength,
                                     double chamferLeft, double chamferRight, QWidget *parent)
    : QDialog(parent), m_index(index), m_segCount(segCount),
      m_segDiameter(segDiameter), m_segLength(segLength),
      m_chamferLeft(chamferLeft), m_chamferRight(chamferRight)
{
    setWindowTitle(tr("키홈 — %1행 (Ø%2 × %3)").arg(index + 1).arg(num(segDiameter)).arg(num(segLength)));

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QFormLayout *form = new QFormLayout;

    // 형태
    m_open   = new QRadioButton(tr("끝 열림"), this);
    m_closed = new QRadioButton(tr("막힘 (양끝 반원)"), this);
    QHBoxLayout *typeRow = new QHBoxLayout;
    typeRow->addWidget(m_open);
    typeRow->addWidget(m_closed);
    typeRow->addStretch(1);
    form->addRow(tr("형태 :"), typeRow);
    const bool anyOpen = canOpen(ShaftKeywayRef::Left) || canOpen(ShaftKeywayRef::Right);
    m_open->setEnabled(anyOpen);
    if (!anyOpen)
        m_open->setToolTip(tr("끝 열림은 첫 구간·마지막 구간에서만 고를 수 있습니다."));

    // 위치
    m_ref = new QComboBox(this);
    m_ref->addItems({tr("구간 중앙"), tr("구간 왼쪽 면"), tr("구간 오른쪽 면")});
    m_offset = makeSpin(this, 0.0);
    QHBoxLayout *posRow = new QHBoxLayout;
    posRow->addWidget(new QLabel(tr("기준"), this));
    posRow->addWidget(m_ref, 1);
    posRow->addWidget(new QLabel(tr("거리"), this));
    posRow->addWidget(m_offset);
    form->addRow(tr("위치 :"), posRow);

    m_length = makeSpin(this, 0.001);
    form->addRow(tr("길이 (반원 포함 전체) :"), m_length);
    mainLayout->addLayout(form);

    // 홈 크기
    QGroupBox *sizeBox = new QGroupBox(tr("홈 크기"), this);
    QFormLayout *sizeForm = new QFormLayout(sizeBox);
    QPushButton *stdBtn = new QPushButton(tr("KS 평행키 표준값"), sizeBox);
    double sw = 0.0, sd = 0.0;
    if (ShaftKeyway::standardSize(segDiameter, sw, sd)) {
        stdBtn->setToolTip(tr("Ø%1 → 키 폭 %2, 축 홈 깊이 t1 = %3").arg(num(segDiameter)).arg(num(sw)).arg(num(sd)));
    } else {
        stdBtn->setEnabled(false);
        stdBtn->setToolTip(tr("KS 평행키 표의 축 지름 범위(6~130) 밖입니다."));
    }
    sizeForm->addRow(stdBtn);
    m_upper  = makeSpin(this, 0.001);
    m_center = makeSpin(this, -100000.0);
    m_lower  = makeSpin(this, 0.001);
    m_depth  = makeSpin(this, 0.001);
    sizeForm->addRow(tr("상단 (홈 중앙선 → 위쪽 벽) :"), m_upper);
    sizeForm->addRow(tr("중앙 (축 중심선 → 홈 중앙선, 위쪽 +) :"), m_center);
    sizeForm->addRow(tr("하단 (홈 중앙선 → 아래쪽 벽) :"), m_lower);
    sizeForm->addRow(tr("홈 깊이 (축 윗면 → 홈 바닥, 단면·3D) :"), m_depth);
    mainLayout->addWidget(sizeBox);

    m_section = new QCheckBox(tr("단면으로 표시 (위쪽 절반을 해칭 단면으로)"), this);
    mainLayout->addWidget(m_section);

    m_info = new QLabel(this);
    m_info->setWordWrap(true);
    mainLayout->addWidget(m_info);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(m_buttons);

    connect(stdBtn, &QPushButton::clicked, this, &ShaftKeywayDialog::applyStandard);
    connect(m_open,    &QRadioButton::toggled, this, &ShaftKeywayDialog::updateState);
    connect(m_closed,  &QRadioButton::toggled, this, &ShaftKeywayDialog::updateState);
    connect(m_ref, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ShaftKeywayDialog::updateState);
    connect(m_section, &QCheckBox::toggled, this, &ShaftKeywayDialog::updateState);
    QDoubleSpinBox *spins[] = {m_offset, m_length, m_upper, m_center, m_lower, m_depth};
    for (QDoubleSpinBox *sp : spins)
        connect(sp, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShaftKeywayDialog::updateState);

    setKeyway(defaultFor(index, segCount, segDiameter, segLength));
}

bool ShaftKeywayDialog::canOpen(ShaftKeywayRef ref) const {
    if (ref == ShaftKeywayRef::Left)  return m_index == 0;
    if (ref == ShaftKeywayRef::Right) return m_index == m_segCount - 1;
    return false;
}

ShaftKeyway ShaftKeywayDialog::defaultFor(int index, int segCount, double segDiameter, double segLength) {
    ShaftKeyway k;
    k.enabled = true;
    double w = 5.0, d = 3.0;
    ShaftKeyway::standardSize(segDiameter, w, d);
    k.upper = k.lower = w * 0.5;
    k.center = 0.0;
    k.sectionDepth = d;
    k.offset = 0.0;
    if (index == 0) {
        k.type = ShaftKeywayType::Open;
        k.ref  = ShaftKeywayRef::Left;
    } else if (index == segCount - 1) {
        k.type = ShaftKeywayType::Open;
        k.ref  = ShaftKeywayRef::Right;
    } else {
        k.type = ShaftKeywayType::Closed;
        k.ref  = ShaftKeywayRef::Center;
    }
    k.length = qMax(w, std::floor(segLength * 0.7));
    return k;
}

void ShaftKeywayDialog::setKeyway(const ShaftKeyway &k) {
    QWidget *widgets[] = {m_open, m_closed, m_ref, m_offset, m_length, m_upper, m_center, m_lower, m_depth,
                          m_section};
    for (QWidget *w : widgets) w->blockSignals(true);
    const bool open = k.type == ShaftKeywayType::Open &&
                      (canOpen(ShaftKeywayRef::Left) || canOpen(ShaftKeywayRef::Right));
    m_open->setChecked(open);
    m_closed->setChecked(!open);
    m_ref->setCurrentIndex(int(k.ref));
    m_offset->setValue(k.offset);
    m_length->setValue(k.length);
    m_upper->setValue(k.upper);
    m_center->setValue(k.center);
    m_lower->setValue(k.lower);
    m_depth->setValue(k.sectionDepth);
    m_section->setChecked(k.showSection);
    for (QWidget *w : widgets) w->blockSignals(false);
    updateState();
}

ShaftKeyway ShaftKeywayDialog::keyway() const {
    ShaftKeyway k;
    k.enabled = true;
    k.type   = m_open->isChecked() ? ShaftKeywayType::Open : ShaftKeywayType::Closed;
    const int r = m_ref->currentIndex();
    k.ref    = (r == 1) ? ShaftKeywayRef::Left : (r == 2) ? ShaftKeywayRef::Right : ShaftKeywayRef::Center;
    k.offset = (k.type == ShaftKeywayType::Open || k.ref == ShaftKeywayRef::Center) ? 0.0 : m_offset->value();
    k.length = m_length->value();
    k.upper  = m_upper->value();
    k.center = m_center->value();
    k.lower  = m_lower->value();
    k.sectionDepth = m_depth->value();
    k.showSection  = m_section->isChecked();
    return k;
}

void ShaftKeywayDialog::applyStandard() {
    double w = 0.0, d = 0.0;
    if (!ShaftKeyway::standardSize(m_segDiameter, w, d)) return;
    m_upper->setValue(w * 0.5);
    m_lower->setValue(w * 0.5);
    m_center->setValue(0.0);
    m_depth->setValue(d);
}

void ShaftKeywayDialog::updateState() {
    const bool open = m_open->isChecked();

    // 기준 콤보: 끝 열림이면 "열린 쪽" 선택 — 중앙과 열 수 없는 쪽은 끈다.
    QStandardItemModel *model = qobject_cast<QStandardItemModel *>(m_ref->model());
    if (model) {
        model->item(0)->setEnabled(!open);
        model->item(1)->setEnabled(!open || canOpen(ShaftKeywayRef::Left));
        model->item(2)->setEnabled(!open || canOpen(ShaftKeywayRef::Right));
    }
    if (open) {
        const int cur = m_ref->currentIndex();
        const bool ok = (cur == 1 && canOpen(ShaftKeywayRef::Left)) || (cur == 2 && canOpen(ShaftKeywayRef::Right));
        if (!ok) {
            m_ref->blockSignals(true);
            m_ref->setCurrentIndex(canOpen(ShaftKeywayRef::Left) ? 1 : 2);
            m_ref->blockSignals(false);
        }
    }
    const int ref = m_ref->currentIndex();
    m_offset->setEnabled(!open && ref != 0);

    // 안내 + 검사
    const ShaftKeyway k = keyway();
    const double L = m_segLength;
    double sa = 0.0, sb = 0.0;
    if (open) {
        if (ref == 2) { sb = L; sa = L - k.length; }
        else          { sa = 0.0; sb = k.length; }
    } else if (ref == 1) { sa = m_chamferLeft + k.offset; sb = sa + k.length; }
    else if (ref == 2)   { sb = L - m_chamferRight - k.offset; sa = sb - k.length; }
    else                 { sa = (L - k.length) * 0.5; sb = sa + k.length; }

    QString text = tr("홈 폭 %1 mm · 구간 Ø%2 × %3 mm").arg(num(k.width())).arg(num(m_segDiameter)).arg(num(L));
    if (open)
        text += tr("\n끝면에서 시작, 안쪽 여유 %1 mm").arg(num(ref == 2 ? sa : L - sb));
    else
        text += tr("\n구간 안 위치 %1 ~ %2 mm (왼쪽 여유 %3, 오른쪽 여유 %4)")
                    .arg(num(sa)).arg(num(sb)).arg(num(sa)).arg(num(L - sb));

    const double shift = ShaftBuilder::keywayChamferShift(k, m_chamferLeft, m_chamferRight);
    if (shift > 1e-9)
        text += tr("\n기준면 모따기 C%1 만큼 안쪽에서 잽니다.").arg(num(shift));
    const QStringList problems = ShaftBuilder::validateKeyway(k, m_index, m_segCount, m_segDiameter, m_segLength,
                                                              m_chamferLeft, m_chamferRight);
    if (!problems.isEmpty()) {
        text += QStringLiteral("\n") + problems.join(QLatin1Char('\n'));
        m_info->setStyleSheet(QStringLiteral("color: #c62828;"));
    } else {
        m_info->setStyleSheet(QString());
    }
    m_info->setText(text);
    if (m_buttons)
        m_buttons->button(QDialogButtonBox::Ok)->setEnabled(problems.isEmpty());
}

// ═════════════════════════════════════════════════════════════════════════
// ShaftThreadDialog
// ═════════════════════════════════════════════════════════════════════════
ShaftThreadDialog::ShaftThreadDialog(ShaftThreadSide side, double leftD, double leftL, double rightD, double rightL,
                                     QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("나사부"));
    m_d[0] = leftD;  m_l[0] = leftL;
    m_d[1] = rightD; m_l[1] = rightL;

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QGridLayout *grid = new QGridLayout;
    grid->addWidget(new QLabel(tr("나사 길이"), this), 0, 1);
    grid->addWidget(new QLabel(tr("피치 (0=표준)"), this), 0, 2);
    const QString names[2] = {tr("왼쪽 :"), tr("오른쪽 :")};
    for (int i = 0; i < 2; ++i) {
        m_len[i]   = makeSpin(this, 0.001);
        m_pitch[i] = makeSpin(this, 0.0, 100.0);
        m_spec[i]  = new QLabel(this);
        grid->addWidget(new QLabel(names[i], this), i + 1, 0);
        grid->addWidget(m_len[i], i + 1, 1);
        grid->addWidget(m_pitch[i], i + 1, 2);
        grid->addWidget(m_spec[i], i + 1, 3);
        const bool on = (side == ShaftThreadSide::Both) ||
                        (i == 0 ? side == ShaftThreadSide::Left : side == ShaftThreadSide::Right);
        m_len[i]->setEnabled(on);
        m_pitch[i]->setEnabled(on);
        m_spec[i]->setEnabled(on);
        connect(m_len[i],   QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShaftThreadDialog::updateInfo);
        connect(m_pitch[i], QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ShaftThreadDialog::updateInfo);
    }
    mainLayout->addLayout(grid);

    QLabel *hint = new QLabel(tr("호칭경은 끝 구간 지름입니다. 나사 길이가 끝 구간보다 짧으면 부분나사로 그립니다."), this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    QDialogButtonBox *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttons);
}

void ShaftThreadDialog::setThreads(const ShaftThread &left, const ShaftThread &right) {
    m_len[0]->setValue(left.length);
    m_pitch[0]->setValue(left.pitch);
    m_len[1]->setValue(right.length);
    m_pitch[1]->setValue(right.pitch);
    updateInfo();
}

ShaftThread ShaftThreadDialog::leftThread() const {
    ShaftThread t;
    t.length = m_len[0]->value();
    t.pitch  = m_pitch[0]->value();
    return t;
}

ShaftThread ShaftThreadDialog::rightThread() const {
    ShaftThread t;
    t.length = m_len[1]->value();
    t.pitch  = m_pitch[1]->value();
    return t;
}

void ShaftThreadDialog::updateInfo() {
    for (int i = 0; i < 2; ++i) {
        ShaftThread t;
        t.length = m_len[i]->value();
        t.pitch  = m_pitch[i]->value();
        const double p = ShaftParams::effectivePitch(t, m_d[i]);
        QString s = tr("M%1 · P%2 · 골지름 %3").arg(num(m_d[i])).arg(num(p)).arg(num(m_d[i] - 1.082532 * p));
        if (t.length > m_l[i] + 1e-9)
            s += tr(" (끝 구간 %1 보다 김)").arg(num(m_l[i]));
        m_spec[i]->setText(s);
    }
}
