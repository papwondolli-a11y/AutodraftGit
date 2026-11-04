#include "plandoordoublesetupdialog.h"
#include "spacefinishgroupbox.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPolygonF>
#include <QPushButton>
#include <QRadioButton>
#include <QVBoxLayout>

// ── 잼브 단면 미리보기 아이콘 생성 (PlanDoorSetupDialog 의 makeJambIcon 과 동일 단면) ──
static QPixmap makeDoubleJambIcon(int type, int w, int h)
{
    QPixmap pix(w, h);
    pix.fill(QColor(35, 35, 35));

    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(QPen(QColor(80, 200, 80), 1.5));
    p.setBrush(QColor(55, 55, 55));

    const double J = 45.0, S = 30.0, K = 40.0, V = 200.0;
    const int mg = 3;
    const double sx = double(w - 2 * mg) / J;
    const double sy = double(h - 2 * mg) / V;

    // 람다 대신 지역 함수 대체용 좌표 변환은 QPointF 값을 직접 계산하여 사용
    QVector<QPointF> pts;
    switch (type) {
    case 1:
        pts << QPointF(mg + 0*sx, h-mg-0*sy) << QPointF(mg + 0*sx, h-mg-V*sy)
            << QPointF(mg + S*sx, h-mg-V*sy) << QPointF(mg + S*sx, h-mg-(V-K)*sy)
            << QPointF(mg + J*sx, h-mg-(V-K)*sy) << QPointF(mg + J*sx, h-mg-0*sy);
        break;
    case 2:
        pts << QPointF(mg + 0*sx, h-mg-0*sy) << QPointF(mg + 0*sx, h-mg-V*sy)
            << QPointF(mg + S*sx, h-mg-V*sy) << QPointF(mg + S*sx, h-mg-(V-K)*sy)
            << QPointF(mg + J*sx, h-mg-(V-K)*sy) << QPointF(mg + J*sx, h-mg-K*sy)
            << QPointF(mg + S*sx, h-mg-0*sy);
        break;
    case 3:
        pts << QPointF(mg + 0*sx, h-mg-0*sy) << QPointF(mg + 0*sx, h-mg-V*sy)
            << QPointF(mg + J*sx, h-mg-V*sy) << QPointF(mg + J*sx, h-mg-0*sy);
        break;
    default:
        pts << QPointF(mg + 0*sx, h-mg-0*sy) << QPointF(mg + 0*sx, h-mg-V*sy)
            << QPointF(mg + S*sx, h-mg-V*sy) << QPointF(mg + S*sx, h-mg-(V-K)*sy)
            << QPointF(mg + J*sx, h-mg-(V-K)*sy) << QPointF(mg + J*sx, h-mg-K*sy)
            << QPointF(mg + S*sx, h-mg-K*sy) << QPointF(mg + S*sx, h-mg-0*sy);
        break;
    }
    p.drawPolygon(QPolygonF(pts));
    return pix;
}

// ── 생성자 ───────────────────────────────────────────────────────────────────
PlanDoorDoubleSetupDialog::PlanDoorDoubleSetupDialog(
    double h1, double h2, double v, bool useWallThickness, int frameType,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("쌍여닫이 설정"));
    setModal(true);

    // ── [문 크기] 그룹 ───────────────────────────────────────────────────────
    // 문1(시작점 P1측)·문2 너비를 각각 별도로 입력 (기본값 900mm씩, 합 1800mm)
    m_spinH1 = new QDoubleSpinBox(this);
    m_spinH1->setRange(100.0, 4000.0);
    m_spinH1->setDecimals(1);
    m_spinH1->setSingleStep(10.0);
    m_spinH1->setSuffix(tr(" mm"));
    m_spinH1->setValue(h1);

    m_spinH2 = new QDoubleSpinBox(this);
    m_spinH2->setRange(100.0, 4000.0);
    m_spinH2->setDecimals(1);
    m_spinH2->setSingleStep(10.0);
    m_spinH2->setSuffix(tr(" mm"));
    m_spinH2->setValue(h2);

    m_spinV = new QDoubleSpinBox(this);
    m_spinV->setRange(50.0, 1000.0);
    m_spinV->setDecimals(1);
    m_spinV->setSingleStep(10.0);
    m_spinV->setSuffix(tr(" mm"));
    m_spinV->setValue(v);

    // V-모드 라디오 (입력값 = 초기 활성)
    QWidget     *vModeWidget = new QWidget(this);
    QHBoxLayout *vModeRow    = new QHBoxLayout(vModeWidget);
    vModeRow->setContentsMargins(0, 0, 0, 0);
    m_radioVInput = new QRadioButton(tr("입력값"),    vModeWidget);
    m_radioVWall  = new QRadioButton(tr("벽 두께 값"), vModeWidget);
    vModeRow->addWidget(m_radioVInput);
    vModeRow->addWidget(m_radioVWall);
    vModeRow->addStretch();

    if (useWallThickness) {
        m_radioVWall->setChecked(true);
        m_spinV->setEnabled(false);
    } else {
        m_radioVInput->setChecked(true);   // 초기 활성화
    }

    QFormLayout *sizeForm = new QFormLayout;
    sizeForm->setLabelAlignment(Qt::AlignRight);
    sizeForm->addRow(tr("문1 너비 (시작점 P1측) :"), m_spinH1);
    sizeForm->addRow(tr("문2 너비 :"),                m_spinH2);
    sizeForm->addRow(tr("H (문틀 폭) :"),             m_spinV);
    sizeForm->addRow(tr(""),                           vModeWidget);

    QGroupBox *sizeGroup = new QGroupBox(tr("문 크기"), this);
    sizeGroup->setLayout(sizeForm);

    // ── [문틀 형태] 그룹 ─────────────────────────────────────────────────────
    const int ICW = 38, ICH = 56;
    QButtonGroup *ftBtnGroup = new QButtonGroup(this);
    QHBoxLayout  *ftRow      = new QHBoxLayout;
    ftRow->setSpacing(4);

    for (int i = 0; i < 4; ++i) {
        QVBoxLayout *col = new QVBoxLayout;
        col->setSpacing(2);
        col->setAlignment(Qt::AlignHCenter);

        QLabel *icon = new QLabel(this);
        icon->setPixmap(makeDoubleJambIcon(i, ICW, ICH));
        icon->setAlignment(Qt::AlignCenter);

        m_radioFrameType[i] = new QRadioButton(tr("Type %1").arg(i + 1), this);
        ftBtnGroup->addButton(m_radioFrameType[i], i);

        col->addWidget(icon);
        col->addWidget(m_radioFrameType[i]);
        ftRow->addLayout(col);
    }
    m_radioFrameType[qBound(0, frameType, 3)]->setChecked(true);

    QGroupBox *ftGroup = new QGroupBox(tr("문틀 형태"), this);
    ftGroup->setLayout(ftRow);

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
    mainLayout->addWidget(ftGroup);
    mainLayout->addWidget(m_spaceFinishGroup);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(btnLayout);

    // 코딩 규칙: 람다 금지 → 멤버 슬롯 함수로 연결
    connect(m_btnOk,     &QPushButton::clicked,
            this, &PlanDoorDoubleSetupDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked,
            this, &PlanDoorDoubleSetupDialog::onCancelClicked);
    connect(m_radioVInput, &QRadioButton::toggled,
            this, &PlanDoorDoubleSetupDialog::onVModeInputToggled);
}

// ── 접근자 ───────────────────────────────────────────────────────────────────
double  PlanDoorDoubleSetupDialog::h1()               const { return m_spinH1->value(); }
double  PlanDoorDoubleSetupDialog::h2()               const { return m_spinH2->value(); }
double  PlanDoorDoubleSetupDialog::v()                const { return m_spinV->value(); }
bool    PlanDoorDoubleSetupDialog::useWallThickness() const { return m_radioVWall->isChecked(); }
int PlanDoorDoubleSetupDialog::frameType() const
{
    for (int i = 0; i < 4; ++i)
        if (m_radioFrameType[i]->isChecked())
            return i;
    return 0;
}

// ── 슬롯 ────────────────────────────────────────────────────────────────────
void PlanDoorDoubleSetupDialog::onOkClicked()     { accept(); }
void PlanDoorDoubleSetupDialog::onCancelClicked() { reject(); }

void PlanDoorDoubleSetupDialog::onVModeInputToggled(bool checked)
{
    m_spinV->setEnabled(checked);
}

void PlanDoorDoubleSetupDialog::setSpaceFinishType(int type) { m_spaceFinishGroup->setType(type); }
int  PlanDoorDoubleSetupDialog::spaceFinishType() const         { return m_spaceFinishGroup->type(); }
