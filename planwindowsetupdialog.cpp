#include "planwindowsetupdialog.h"
#include "spacefinishgroupbox.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QRadioButton>
#include <QPushButton>
#include <QColor>
#include <QIcon>
#include <QPixmap>
#include <QVBoxLayout>

// ── 생성자 ───────────────────────────────────────────────────────────────────
PlanWindowSetupDialog::PlanWindowSetupDialog(
    double w,
    bool glass, bool isDouble, double sashW, int sashes,
    bool topOnly, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("평면 창문 설정"));
    setModal(true);

    // ── [창문 크기] 그룹 ──────────────────────────────────────────────────────
    m_spinW = new QDoubleSpinBox(this);
    m_spinW->setRange(100.0, 10000.0);
    m_spinW->setDecimals(1);
    m_spinW->setSingleStep(10.0);
    m_spinW->setSuffix(tr(" mm"));
    m_spinW->setValue(w);

    QFormLayout *sizeForm = new QFormLayout;
    sizeForm->setLabelAlignment(Qt::AlignRight);
    sizeForm->addRow(tr("W (창문 넓이) :"),  m_spinW);

    // 슬라이딩 창문(왼쪽 짝) 넓이 — 0 이면 두 짝이 같은 크기
    m_spinSashW = new QDoubleSpinBox(this);
    m_spinSashW->setRange(0.0, 10000.0);
    m_spinSashW->setDecimals(1);
    m_spinSashW->setSingleStep(10.0);
    m_spinSashW->setSuffix(tr(" mm"));
    m_spinSashW->setSpecialValueText(sashes == 4 ? tr("같은 크기 (4개)")
                                     : sashes == 3 ? tr("같은 크기 (3개)") : tr("같은 크기"));
    m_spinSashW->setValue(sashW);
    sizeForm->addRow(sashes == 4 ? tr("슬라이딩 창문 넓이 (안쪽창 1개) :")
                       : sashes == 3 ? tr("슬라이딩 창문 넓이 (중앙창) :")
                                     : tr("슬라이딩 창문 넓이 :"), m_spinSashW);

    // 유리표현 + 단창/이중창 선택
    m_chkGlass = new QCheckBox(tr("유리표현"), this);
    m_chkGlass->setChecked(glass);
    m_radioSingle = new QRadioButton(tr("단창"), this);
    m_radioDouble = new QRadioButton(tr("복창"), this);
    if (isDouble) m_radioDouble->setChecked(true);
    else          m_radioSingle->setChecked(true);

    QWidget     *typeWidget = new QWidget(this);
    QHBoxLayout *typeRow    = new QHBoxLayout(typeWidget);
    typeRow->setContentsMargins(0, 0, 0, 0);
    typeRow->addWidget(m_chkGlass);
    typeRow->addSpacing(12);
    typeRow->addWidget(m_radioSingle);
    typeRow->addWidget(m_radioDouble);
    typeRow->addStretch();
    sizeForm->addRow(QString(), typeWidget);

    QGroupBox *sizeGroup = new QGroupBox(tr("창문 크기 설정"), this);
    sizeGroup->setLayout(sizeForm);

    // 상단부만 생성하는 창: 단창/복창은 사용하지 않으므로 블라인드 처리
    if (topOnly) {
        setWindowTitle(tr("평면 창문 설정 (상단부)"));
        m_radioSingle->setChecked(true);     // 단창만 선택 가능
        m_radioDouble->setEnabled(false);
    }

    // [공간벽 마감형태] 그룹 (swtype1~4, 초기 swtype1)
    m_spaceFinishGroup = new SpaceFinishGroupBox(this);

    // ── 버튼 ────────────────────────────────────────────────────────────────
    m_btnOk     = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);

    QHBoxLayout *btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    btnLayout->addWidget(m_btnOk);
    btnLayout->addWidget(m_btnCancel);

    // ── 메인 레이아웃 ────────────────────────────────────────────────────────
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(sizeGroup);
    mainLayout->addWidget(m_spaceFinishGroup);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(btnLayout);

    // 람다가 아닌 멤버 함수 포인터 방식의 connect
    connect(m_btnOk, &QPushButton::clicked,
            this, &PlanWindowSetupDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked,
            this, &PlanWindowSetupDialog::onCancelClicked);
}

// ── 접근자 ───────────────────────────────────────────────────────────────────
double  PlanWindowSetupDialog::w()                const { return m_spinW->value(); }
double  PlanWindowSetupDialog::sashW() const { return m_spinSashW->value(); }
bool    PlanWindowSetupDialog::isDouble() const { return m_radioDouble->isChecked(); }
bool    PlanWindowSetupDialog::glass() const { return m_chkGlass->isChecked(); }

// ── 슬롯 ────────────────────────────────────────────────────────────────────
void PlanWindowSetupDialog::onOkClicked()     { accept(); }
void PlanWindowSetupDialog::onCancelClicked() { reject(); }

void PlanWindowSetupDialog::setSpaceFinishType(int type) { m_spaceFinishGroup->setType(type); }
int  PlanWindowSetupDialog::spaceFinishType() const         { return m_spaceFinishGroup->type(); }
