#include "plandoorsetupdialog.h"
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

// ── 잼브 단면 미리보기 아이콘 생성 ──────────────────────────────────────────
QPixmap makeJambIcon(int type, int w, int h)
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

    auto ti = [&](double x, double y) -> QPointF {
        return QPointF(mg + x * sx, h - mg - y * sy);
    };

    QVector<QPointF> pts;
    switch (type) {
    case 1:
        pts << ti(0,0) << ti(0,V) << ti(S,V) << ti(S,V-K) << ti(J,V-K) << ti(J,0);
        break;
    case 2:
        pts << ti(0,0) << ti(0,V) << ti(S,V) << ti(S,V-K)
            << ti(J,V-K) << ti(J,K) << ti(S,0);
        break;
    case 3:
        pts << ti(0,0) << ti(0,V) << ti(J,V) << ti(J,0);
        break;
    default:
        pts << ti(0,0) << ti(0,V) << ti(S,V) << ti(S,V-K)
            << ti(J,V-K) << ti(J,K) << ti(S,K) << ti(S,0);
        break;
    }
    p.drawPolygon(QPolygonF(pts));
    return pix;
}

// ── 생성자 ───────────────────────────────────────────────────────────────────
PlanDoorSetupDialog::PlanDoorSetupDialog(
    double h, double v, bool useWallThickness, int frameType,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("평면도어 설정"));
    setModal(true);

    // ── [문 크기] 그룹 ───────────────────────────────────────────────────────
    m_spinH = new QDoubleSpinBox(this);
    m_spinH->setRange(100.0, 5000.0);
    m_spinH->setDecimals(1);
    m_spinH->setSingleStep(10.0);
    m_spinH->setSuffix(tr(" mm"));
    m_spinH->setValue(h);

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
    sizeForm->addRow(tr("W  (문 너비) :"),   m_spinH);
    sizeForm->addRow(tr("H (문틀 폭) :"), m_spinV);
    sizeForm->addRow(tr(""),                  vModeWidget);

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
        icon->setPixmap(makeJambIcon(i, ICW, ICH));
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

    // ── [공간벽 마감형태] 그룹 (swtype1~4, 초기 swtype1) ─────────────────────
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

    connect(m_btnOk,     &QPushButton::clicked,
            this, &PlanDoorSetupDialog::onOkClicked);
    connect(m_btnCancel, &QPushButton::clicked,
            this, &PlanDoorSetupDialog::onCancelClicked);
    connect(m_radioVInput, &QRadioButton::toggled,
            this, &PlanDoorSetupDialog::onVModeInputToggled);
}

// ── 접근자 ───────────────────────────────────────────────────────────────────
double  PlanDoorSetupDialog::h()                const { return m_spinH->value(); }
double  PlanDoorSetupDialog::v()                const { return m_spinV->value(); }
bool    PlanDoorSetupDialog::useWallThickness() const { return m_radioVWall->isChecked(); }
int PlanDoorSetupDialog::frameType() const
{
    for (int i = 0; i < 4; ++i)
        if (m_radioFrameType[i]->isChecked())
            return i;
    return 0;
}

void PlanDoorSetupDialog::setSpaceFinishType(int type) { m_spaceFinishGroup->setType(type); }
int  PlanDoorSetupDialog::spaceFinishType() const         { return m_spaceFinishGroup->type(); }

// ── 슬롯 ────────────────────────────────────────────────────────────────────
void PlanDoorSetupDialog::onOkClicked()     { accept(); }
void PlanDoorSetupDialog::onCancelClicked() { reject(); }

void PlanDoorSetupDialog::onVModeInputToggled(bool checked)
{
    m_spinV->setEnabled(checked);
}
