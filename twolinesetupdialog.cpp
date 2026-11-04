#include "twolinesetupdialog.h"
#include "layer.h"
#include "layermanager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLineEdit>
#include <QPixmap>
#include <QVBoxLayout>

TwoLineSetupDialog::TwoLineSetupDialog(double currentSpacing,
                                       const QString &currentLayerName,
                                       bool centerLineEnabled,
                                       bool showMortar,
                                       double mortarSpacingVal,
                                       const QString &mortarLayerName,
                                       bool showSpace,
                                       double spaceSpacingVal,
                                       const QString &spaceLayerName,
                                       bool showEccentric,
                                       double eccentricOffsetVal,
                                       double innerWallThicknessVal,
                                       double spaceThicknessVal,
                                       double outerWallThicknessVal,
                                       bool innerMortarEnabledVal,
                                       bool outerMortarEnabledVal,
                                       LayerManager *layerMgr,
                                       QWidget *parent)
    : QDialog(parent)
{
    // 편심6-Line: showSpace && showEccentric 조합 → 완전히 다른 UI
    const bool isEccentric6 = (showSpace && showEccentric);

    // ── 다이얼로그 제목 결정 ────────────────────────────────────────────────────
    QString title;
    if (isEccentric6)          title = tr("편심6-Line 설정");
    else if (showSpace)        title = tr("6-Line 설정");
    else if (showMortar)       title = tr("4-Line 설정");
    else if (showEccentric)    title = tr("편심2-Line 설정");
    else                       title = tr("2-Line 설정");
    setWindowTitle(title);

    // ── 다이얼로그 높이 결정 ────────────────────────────────────────────────────
    // isEccentric6: 선간격(4행)+중심선(2행)+몰타르(4행)+편심(1행) 그룹 구성
    // 각 그룹은 그룹박스 제목(~25px) + 행당 ~37px + 하단 마진 ~10px
    // 2행 그룹 ≈ 95px, 1행 그룹 ≈ 65px, 추가 행당 +37px
    const int height = isEccentric6                     ? 553
                     : (showSpace  && showEccentric)    ? 488  // (isEccentric6로 도달 안됨)
                     : showSpace                        ? 497  // 몰타르 체크 2행(+74) 포함
                     : (showMortar && showEccentric)    ? 472
                     : showMortar                       ? 407
                     : showEccentric                    ? 303
                                                        : 243;
    setFixedSize(300, height);

    // ════════════════════════════════════════════════════════════════
    // 레이어 콤보 (공통: 외벽선/선간격 그룹에서 사용)
    // ════════════════════════════════════════════════════════════════
    m_layerCombo = new QComboBox(this);
    m_layerCombo->setObjectName("TwoLineLayerCombo");
    m_layerCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_layerCombo->setMaximumHeight(33);
    m_layerCombo->setEditable(true);
    if (m_layerCombo->lineEdit()) {
        m_layerCombo->lineEdit()->setReadOnly(true);
        m_layerCombo->lineEdit()->setAlignment(Qt::AlignCenter);
    }
    populateCombo(m_layerCombo, layerMgr, currentLayerName);

    // ════════════════════════════════════════════════════════════════
    // 외벽선 / 선간격 설정 그룹
    // 편심6-Line: "선간격 설정" (안쪩 + 공간 + 바깥쪩 + 레이어)
    // 그 외:     "바같선 설정" (두 선 간격 + 레이어)
    // ════════════════════════════════════════════════════════════════
    QGroupBox *outerGroup = nullptr;
    if (isEccentric6) {
        // 편심6-Line 전용: 3개 스핀박스
        m_innerWallSpinBox = new QDoubleSpinBox(this);
        m_innerWallSpinBox->setRange(0.001, 100000.0);
        m_innerWallSpinBox->setDecimals(3);
        m_innerWallSpinBox->setValue(innerWallThicknessVal);
        m_innerWallSpinBox->setSuffix(tr("  (도면 단위)"));

        m_spaceThickSpinBox = new QDoubleSpinBox(this);
        m_spaceThickSpinBox->setRange(0.001, 100000.0);
        m_spaceThickSpinBox->setDecimals(3);
        m_spaceThickSpinBox->setValue(spaceThicknessVal);
        m_spaceThickSpinBox->setSuffix(tr("  (도면 단위)"));

        m_outerWallSpinBox = new QDoubleSpinBox(this);
        m_outerWallSpinBox->setRange(0.001, 100000.0);
        m_outerWallSpinBox->setDecimals(3);
        m_outerWallSpinBox->setValue(outerWallThicknessVal);
        m_outerWallSpinBox->setSuffix(tr("  (도면 단위)"));

        auto *e6Form = new QFormLayout;
        e6Form->addRow(tr("안쪽 :"),     m_innerWallSpinBox);
        e6Form->addRow(tr("공간 :"),     m_spaceThickSpinBox);
        e6Form->addRow(tr("바깥쪽 :"),   m_outerWallSpinBox);
        e6Form->addRow(tr("레이어 설정 :"), m_layerCombo);

        outerGroup = new QGroupBox(tr("선간격 설정"), this);
        outerGroup->setLayout(e6Form);
    } else {
        // 일반 모드: 두 선 간격 + 레이어
        m_spinBox = new QDoubleSpinBox(this);
        m_spinBox->setRange(0.001, 100000.0);
        m_spinBox->setDecimals(3);
        m_spinBox->setValue(currentSpacing);
        m_spinBox->setSuffix(tr("  (도면 단위)"));

        auto *outerForm = new QFormLayout;
        outerForm->addRow(tr("두 선 간격 :"), m_spinBox);
        outerForm->addRow(tr("레이어 설정 :"), m_layerCombo);

        outerGroup = new QGroupBox(tr("바같선 설정"), this);
        outerGroup->setLayout(outerForm);
    }

    // ════════════════════════════════════════════════════════════════
    // 중심선 설정 그룹 (모든 모드 공통)
    // ════════════════════════════════════════════════════════════════
    m_centerLineCheck = new QCheckBox(tr("중심선"), this);
    m_centerLineCheck->setChecked(centerLineEnabled);

    auto *centerForm = new QFormLayout;
    centerForm->addRow(QString(), m_centerLineCheck);

    auto *centerGroup = new QGroupBox(tr("중심선 설정"), this);
    centerGroup->setLayout(centerForm);

    // ════════════════════════════════════════════════════════════════
    // 몰타르선 설정 그룹 (4-Line / 6-Line 전용)
    // 편심6-Line 시: "몰타르선 간격" 아래 내벽/외벽 체크박스 추가
    // ════════════════════════════════════════════════════════════════
    QGroupBox *mortarGroup = nullptr;
    if (showMortar) {
        m_mortarSpinBox = new QDoubleSpinBox(this);
        m_mortarSpinBox->setRange(0.001, 100000.0);
        m_mortarSpinBox->setDecimals(3);
        m_mortarSpinBox->setValue(mortarSpacingVal);
        m_mortarSpinBox->setSuffix(tr("  (도면 단위)"));

        m_mortarLayerCombo = new QComboBox(this);
        m_mortarLayerCombo->setObjectName("TwoLineMortarLayerCombo");
        m_mortarLayerCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_mortarLayerCombo->setMaximumHeight(33);
        m_mortarLayerCombo->setEditable(true);
        if (m_mortarLayerCombo->lineEdit()) {
            m_mortarLayerCombo->lineEdit()->setReadOnly(true);
            m_mortarLayerCombo->lineEdit()->setAlignment(Qt::AlignCenter);
        }
        populateCombo(m_mortarLayerCombo, layerMgr, mortarLayerName);

        auto *mortarForm = new QFormLayout;
        mortarForm->addRow(tr("몰타르선 간격 :"), m_mortarSpinBox);

        // 4-Line / 6-Line / 편심4-Line / 편심6-Line 공통: 내벽/외벽 몰타르 개별 활성 체크박스
        m_innerMortarCheck = new QCheckBox(tr("내벽 몰타르"), this);
        m_innerMortarCheck->setChecked(innerMortarEnabledVal);
        m_outerMortarCheck = new QCheckBox(tr("외벽 몰타르"), this);
        m_outerMortarCheck->setChecked(outerMortarEnabledVal);
        mortarForm->addRow(QString(), m_innerMortarCheck);
        mortarForm->addRow(QString(), m_outerMortarCheck);

        mortarForm->addRow(tr("레이어 설정 :"), m_mortarLayerCombo);

        mortarGroup = new QGroupBox(tr("몰타르선 설정"), this);
        mortarGroup->setLayout(mortarForm);
    }

    // ════════════════════════════════════════════════════════════════
    // 공간선 설정 그룹 (일반 6-Line 전용; 편심6-Line에서는 생성하지 않음)
    // ════════════════════════════════════════════════════════════════
    QGroupBox *spaceGroup = nullptr;
    if (showSpace && !isEccentric6) {
        m_spaceSpinBox = new QDoubleSpinBox(this);
        m_spaceSpinBox->setRange(0.001, 100000.0);
        m_spaceSpinBox->setDecimals(3);
        m_spaceSpinBox->setValue(spaceSpacingVal);
        m_spaceSpinBox->setSuffix(tr("  (도면 단위)"));

        m_spaceLayerCombo = new QComboBox(this);
        m_spaceLayerCombo->setObjectName("TwoLineSpaceLayerCombo");
        m_spaceLayerCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_spaceLayerCombo->setMaximumHeight(33);
        m_spaceLayerCombo->setEditable(true);
        if (m_spaceLayerCombo->lineEdit()) {
            m_spaceLayerCombo->lineEdit()->setReadOnly(true);
            m_spaceLayerCombo->lineEdit()->setAlignment(Qt::AlignCenter);
        }
        populateCombo(m_spaceLayerCombo, layerMgr,
                      spaceLayerName.isEmpty() ? currentLayerName : spaceLayerName);

        auto *spaceForm = new QFormLayout;
        spaceForm->addRow(tr("공간선 간격 :"), m_spaceSpinBox);
        spaceForm->addRow(tr("레이어 설정 :"), m_spaceLayerCombo);

        spaceGroup = new QGroupBox(tr("공간선 설정"), this);
        spaceGroup->setLayout(spaceForm);
    }

    // ════════════════════════════════════════════════════════════════
    // 편심 설정 그룹 (showEccentric=true 일 때)
    // "중심선 위치 :" — 기준 경로에서 내벽선까지의 거리(mm)
    // 외벽선은 전체 폭에서 자동 계산 (일반 편심) 또는 안쪩/공간/바깥쪩로 결정 (편심6)
    // ════════════════════════════════════════════════════════════════
    QGroupBox *eccentricGroup = nullptr;
    if (showEccentric) {
        m_eccentricSpinBox = new QDoubleSpinBox(this);
        m_eccentricSpinBox->setRange(0.001, 100000.0);
        m_eccentricSpinBox->setDecimals(3);
        m_eccentricSpinBox->setValue(eccentricOffsetVal);
        m_eccentricSpinBox->setSuffix(tr("  (도면 단위)"));

        auto *eccForm = new QFormLayout;
        eccForm->addRow(tr("중심선 위치 :"), m_eccentricSpinBox);

        eccentricGroup = new QGroupBox(tr("편심 설정"), this);
        eccentricGroup->setLayout(eccForm);
    }

    // ════════════════════════════════════════════════════════════════
    // 메인 레이아웃
    // ════════════════════════════════════════════════════════════════
    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(outerGroup);
    mainLayout->addWidget(centerGroup);
    if (mortarGroup)
        mainLayout->addWidget(mortarGroup);
    if (spaceGroup)
        mainLayout->addWidget(spaceGroup);
    if (eccentricGroup)
        mainLayout->addWidget(eccentricGroup);
    mainLayout->addWidget(buttons);
}

// ── 레이어 목록 채우기 (BlockPaletteDock과 동일한 방식) ─────────────────────
void TwoLineSetupDialog::populateCombo(QComboBox *combo, LayerManager *mgr,
                                       const QString &selectedName)
{
    combo->blockSignals(true);
    combo->clear();
    combo->addItem(tr("기본값 (현재 레이어)"), QString());

    if (mgr) {
        for (const Layer &layer : mgr->getAllLayers()) {
            QPixmap px(16, 16);
            px.fill(layer.color());
            combo->addItem(QIcon(px), layer.name(), layer.name());
        }
    }

    int idx = selectedName.isEmpty() ? 0 : combo->findData(selectedName);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
    combo->blockSignals(false);
}

// ── 일반 게터 ───────────────────────────────────────────────────────────────

double TwoLineSetupDialog::spacing() const
{
    // 편심6-Line 모드에서는 m_spinBox가 nullptr → 0.0 반환 (s_spacing 미사용)
    return m_spinBox ? m_spinBox->value() : 0.0;
}

QString TwoLineSetupDialog::selectedLayerName() const
{
    return m_layerCombo ? m_layerCombo->currentData().toString() : QString();
}

bool TwoLineSetupDialog::centerLineEnabled() const
{
    return m_centerLineCheck ? m_centerLineCheck->isChecked() : false;
}

double TwoLineSetupDialog::mortarSpacing() const
{
    return m_mortarSpinBox ? m_mortarSpinBox->value() : 0.0;
}

QString TwoLineSetupDialog::mortarLayerName() const
{
    return m_mortarLayerCombo ? m_mortarLayerCombo->currentData().toString() : QString();
}

double TwoLineSetupDialog::spaceSpacing() const
{
    // 편심6-Line 모드에서는 m_spaceSpinBox가 없음 → 0.0 반환
    return m_spaceSpinBox ? m_spaceSpinBox->value() : 0.0;
}

QString TwoLineSetupDialog::spaceLayerName() const
{
    return m_spaceLayerCombo ? m_spaceLayerCombo->currentData().toString() : QString();
}

double TwoLineSetupDialog::eccentricOffset() const
{
    return m_eccentricSpinBox ? m_eccentricSpinBox->value() : 0.0;
}

// ── 편심6-Line 전용 게터 ────────────────────────────────────────────────────

double TwoLineSetupDialog::innerWallThickness() const
{
    return m_innerWallSpinBox ? m_innerWallSpinBox->value() : 0.0;
}

double TwoLineSetupDialog::spaceThickness() const
{
    return m_spaceThickSpinBox ? m_spaceThickSpinBox->value() : 0.0;
}

double TwoLineSetupDialog::outerWallThickness() const
{
    return m_outerWallSpinBox ? m_outerWallSpinBox->value() : 0.0;
}

bool TwoLineSetupDialog::innerMortarEnabled() const
{
    // 체크박스가 없는 모드(일반 4-Line)에서는 항상 true (기존 동작 유지)
    return m_innerMortarCheck ? m_innerMortarCheck->isChecked() : true;
}

bool TwoLineSetupDialog::outerMortarEnabled() const
{
    return m_outerMortarCheck ? m_outerMortarCheck->isChecked() : true;
}
