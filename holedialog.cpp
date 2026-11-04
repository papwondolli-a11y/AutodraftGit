#include "holedialog.h"
#include "layermanager.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QVBoxLayout>
#include <algorithm>

HoleDialog::HoleDialog(LayerManager *layerMgr, QWidget *parent)
    : QDialog(parent), m_layerMgr(layerMgr) {
    setWindowTitle(tr("구멍 설정"));
    setupUI();
    setData(MyHole());
}

void HoleDialog::populateLayerCombo(QComboBox *combo, const QString &selectedName) {
    combo->blockSignals(true);
    combo->clear();

    if (m_layerMgr) {
        for (const Layer &layer : m_layerMgr->getAllLayers()) {
            QPixmap px(16, 16);
            px.fill(layer.color());
            combo->addItem(QIcon(px), layer.name(), layer.name());
        }
    }

    int idx = combo->findData(selectedName);
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
    combo->blockSignals(false);
}

void HoleDialog::setupUI() {
    auto *mainLayout = new QVBoxLayout(this);

    // ── 기본 구멍 ──────────────────────────────
    auto *baseGroup = new QGroupBox(tr("기본 구멍"), this);
    auto *baseForm = new QFormLayout(baseGroup);

    m_spnDiameter = new QDoubleSpinBox(baseGroup);
    m_spnDiameter->setRange(0.1, 1000.0);
    m_spnDiameter->setDecimals(2);
    m_spnDiameter->setValue(10.0);
    baseForm->addRow(tr("지름 (Ø)"), m_spnDiameter);
    connect(m_spnDiameter, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &HoleDialog::onBaseDiameterChanged);

    auto *offsetRow = new QHBoxLayout();
    m_spnOffsetX = new QDoubleSpinBox(baseGroup);
    m_spnOffsetX->setRange(-100000.0, 100000.0);
    m_spnOffsetX->setDecimals(2);
    m_spnOffsetX->setValue(0.0);
    m_spnOffsetY = new QDoubleSpinBox(baseGroup);
    m_spnOffsetY->setRange(-100000.0, 100000.0);
    m_spnOffsetY->setDecimals(2);
    m_spnOffsetY->setValue(0.0);
    offsetRow->addWidget(new QLabel(tr("X축"), baseGroup));
    offsetRow->addWidget(m_spnOffsetX);
    offsetRow->addWidget(new QLabel(tr("Y축"), baseGroup));
    offsetRow->addWidget(m_spnOffsetY);
    baseForm->addRow(tr("위치(거리)"), offsetRow);

    auto *arrayLayout = new QVBoxLayout();

    auto *arrayRowX = new QHBoxLayout();
    m_spnArrayCountX = new QSpinBox(baseGroup);
    m_spnArrayCountX->setRange(1, 1000);
    m_spnArrayCountX->setValue(1);
    m_spnArraySpacingX = new QDoubleSpinBox(baseGroup);
    m_spnArraySpacingX->setRange(-100000.0, 100000.0);
    m_spnArraySpacingX->setDecimals(2);
    m_spnArraySpacingX->setValue(0.0);
    arrayRowX->addWidget(new QLabel(tr("X축 갯수"), baseGroup));
    arrayRowX->addWidget(m_spnArrayCountX);
    arrayRowX->addWidget(new QLabel(tr("간격"), baseGroup));
    arrayRowX->addWidget(m_spnArraySpacingX);
    arrayLayout->addLayout(arrayRowX);

    auto *arrayRowY = new QHBoxLayout();
    m_spnArrayCountY = new QSpinBox(baseGroup);
    m_spnArrayCountY->setRange(1, 1000);
    m_spnArrayCountY->setValue(1);
    m_spnArraySpacingY = new QDoubleSpinBox(baseGroup);
    m_spnArraySpacingY->setRange(-100000.0, 100000.0);
    m_spnArraySpacingY->setDecimals(2);
    m_spnArraySpacingY->setValue(0.0);
    arrayRowY->addWidget(new QLabel(tr("Y축 갯수"), baseGroup));
    arrayRowY->addWidget(m_spnArrayCountY);
    arrayRowY->addWidget(new QLabel(tr("간격"), baseGroup));
    arrayRowY->addWidget(m_spnArraySpacingY);
    arrayLayout->addLayout(arrayRowY);

    baseForm->addRow(tr("배열"), arrayLayout);

    auto *throughRow = new QHBoxLayout();
    m_radioThrough = new QRadioButton(tr("관통 (THRU)"), baseGroup);
    m_radioBlind = new QRadioButton(tr("막힌 구멍"), baseGroup);
    m_radioThrough->setChecked(true);
    throughRow->addWidget(m_radioThrough);
    throughRow->addWidget(m_radioBlind);
    baseForm->addRow(tr("관통 여부"), throughRow);

    m_spnDepth = new QDoubleSpinBox(baseGroup);
    m_spnDepth->setRange(0.0, 1000.0);
    m_spnDepth->setDecimals(2);
    m_spnDepth->setEnabled(false);
    baseForm->addRow(tr("깊이 (DP)"), m_spnDepth);
    connect(m_radioBlind, &QRadioButton::toggled, m_spnDepth, &QDoubleSpinBox::setEnabled);
    connect(m_radioBlind, &QRadioButton::toggled, this, &HoleDialog::onBaseThroughToggled);
    connect(m_spnDepth, qOverload<double>(&QDoubleSpinBox::valueChanged), this, &HoleDialog::onBaseDepthChanged);

    m_edTolerance = new QLineEdit(baseGroup);
    m_edTolerance->setPlaceholderText(tr("예: H7 (선택)"));
    baseForm->addRow(tr("공차등급"), m_edTolerance);

    m_cmbBaseLayer = new QComboBox(baseGroup);
    baseForm->addRow(tr("기본구멍 레이어"), m_cmbBaseLayer);

    m_chkCenterLine = new QCheckBox(tr("중심선"), baseGroup);
    m_chkCenterLine->setChecked(true);
    baseForm->addRow(QString(), m_chkCenterLine);

    // 판재 모서리·대칭선에 걸친 구멍을 절반만 표시할 때 쓰는 표시 범위 — 라디오처럼
    // 항상 하나만 켜지도록 onRenderModeToggled에서 상호배타적으로 처리한다.
    auto *renderModeRow = new QHBoxLayout();
    m_chkRenderFull = new QCheckBox(tr("전체"), baseGroup);
    m_chkRenderRight = new QCheckBox(tr("오른쪽"), baseGroup);
    m_chkRenderLeft = new QCheckBox(tr("왼쪽"), baseGroup);
    m_chkRenderFull->setChecked(true);
    renderModeRow->addWidget(m_chkRenderFull);
    renderModeRow->addWidget(m_chkRenderRight);
    renderModeRow->addWidget(m_chkRenderLeft);
    baseForm->addRow(tr("표시 범위"), renderModeRow);
    connect(m_chkRenderFull, &QCheckBox::toggled, this, &HoleDialog::onRenderModeToggled);
    connect(m_chkRenderRight, &QCheckBox::toggled, this, &HoleDialog::onRenderModeToggled);
    connect(m_chkRenderLeft, &QCheckBox::toggled, this, &HoleDialog::onRenderModeToggled);

    mainLayout->addWidget(baseGroup);

    // ── 탭(나사) ──────────────────────────────
    auto *threadGroup = new QGroupBox(tr("탭(나사) 가공"), this);
    threadGroup->setCheckable(false);
    auto *threadForm = new QFormLayout(threadGroup);

    m_chkThread = new QCheckBox(tr("탭 가공 적용"), threadGroup);
    threadForm->addRow(m_chkThread);

    m_cmbThreadSpec = new QComboBox(threadGroup);
    m_cmbThreadSpec->setEditable(true);
    m_cmbThreadSpec->addItems({"M3x0.5", "M4x0.7", "M5x0.8", "M6x1.0", "M8x1.25",
                               "M10x1.5", "M12x1.75", "M14x2.0", "M16x2.0", "M20x2.5"});
    m_cmbThreadSpec->setCurrentText("M10x1.5");
    threadForm->addRow(tr("규격"), m_cmbThreadSpec);
    connect(m_cmbThreadSpec, &QComboBox::currentTextChanged, this, &HoleDialog::onThreadSpecChanged);

    m_edThreadClass = new QLineEdit(threadGroup);
    m_edThreadClass->setText("6H");
    threadForm->addRow(tr("등급"), m_edThreadClass);

    // 탭의 관통/깊이는 기본구멍의 관통여부·깊이에서 그대로 따름(독립 입력 없음).
    // 막힌 구멍일 때만 활성화되고, 나사산이 파인 깊이는 드릴 깊이를 넘을 수 없음.
    m_lblThreadDepth = new QLabel(tr("탭 깊이 (DP)"), threadGroup);
    m_spnThreadDepth = new QDoubleSpinBox(threadGroup);
    m_spnThreadDepth->setRange(0.0, 1000.0);
    m_spnThreadDepth->setDecimals(2);
    m_spnThreadDepth->setEnabled(false);
    threadForm->addRow(m_lblThreadDepth, m_spnThreadDepth);

    m_cmbThreadLayer = new QComboBox(threadGroup);
    threadForm->addRow(tr("탭가공 레이어"), m_cmbThreadLayer);

    mainLayout->addWidget(threadGroup);
    connect(m_chkThread, &QCheckBox::toggled, this, &HoleDialog::onThreadToggled);

    // ── 카운터보어 ──────────────────────────────
    auto *cbGroup = new QGroupBox(tr("카운터보어 (C/B)"), this);
    auto *cbForm = new QFormLayout(cbGroup);

    m_chkCounterbore = new QCheckBox(tr("카운터보어 적용"), cbGroup);
    cbForm->addRow(m_chkCounterbore);

    m_spnCbDiameter = new QDoubleSpinBox(cbGroup);
    m_spnCbDiameter->setRange(0.1, 1000.0);
    m_spnCbDiameter->setDecimals(2);
    m_spnCbDiameter->setValue(16.0);
    m_spnCbDiameter->setMinimum(m_spnDiameter->value()); // 기본구멍 지름보다 작을 수 없음
    cbForm->addRow(tr("지름 (Ø)"), m_spnCbDiameter);

    m_spnCbDepth = new QDoubleSpinBox(cbGroup);
    m_spnCbDepth->setRange(0.0, 1000.0);
    m_spnCbDepth->setDecimals(2);
    m_spnCbDepth->setValue(6.0);
    cbForm->addRow(tr("깊이 (DP)"), m_spnCbDepth);

    m_cmbCbLayer = new QComboBox(cbGroup);
    cbForm->addRow(tr("카운터보어 레이어"), m_cmbCbLayer);

    mainLayout->addWidget(cbGroup);
    connect(m_chkCounterbore, &QCheckBox::toggled, this, &HoleDialog::onCounterboreToggled);

    // ── 카운터싱크 ──────────────────────────────
    auto *csGroup = new QGroupBox(tr("카운터싱크 (C/S)"), this);
    auto *csForm = new QFormLayout(csGroup);

    m_chkCountersink = new QCheckBox(tr("카운터싱크 적용"), csGroup);
    csForm->addRow(m_chkCountersink);

    m_spnCsDiameter = new QDoubleSpinBox(csGroup);
    m_spnCsDiameter->setRange(0.1, 1000.0);
    m_spnCsDiameter->setDecimals(2);
    m_spnCsDiameter->setValue(14.0);
    m_spnCsDiameter->setMinimum(m_spnDiameter->value()); // 기본구멍 지름보다 작을 수 없음
    csForm->addRow(tr("지름 (Ø)"), m_spnCsDiameter);

    m_spnCsAngle = new QDoubleSpinBox(csGroup);
    m_spnCsAngle->setRange(60.0, 120.0);
    m_spnCsAngle->setDecimals(0);
    m_spnCsAngle->setValue(90.0);
    m_spnCsAngle->setSuffix(QStringLiteral("°"));
    csForm->addRow(tr("각도"), m_spnCsAngle);

    m_cmbCsLayer = new QComboBox(csGroup);
    csForm->addRow(tr("카운터싱크 레이어"), m_cmbCsLayer);

    mainLayout->addWidget(csGroup);
    connect(m_chkCountersink, &QCheckBox::toggled, this, &HoleDialog::onCountersinkToggled);

    // ── 콜아웃 표기 방식 ──────────────────────────────
    auto *styleGroup = new QGroupBox(tr("콜아웃 표기 방식"), this);
    auto *styleRow = new QHBoxLayout(styleGroup);
    m_radioStyleText = new QRadioButton(tr("텍스트 (C/B, C/S, DP)"), styleGroup);
    m_radioStyleGraphic = new QRadioButton(tr("기호 (⌴, ⌵)"), styleGroup);
    m_radioStyleText->setChecked(true);
    styleRow->addWidget(m_radioStyleText);
    styleRow->addWidget(m_radioStyleGraphic);
    mainLayout->addWidget(styleGroup);

    // ── OK / Cancel ──────────────────────────────
    auto *btnRow = new QHBoxLayout();
    m_btnOk = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    btnRow->addStretch();
    btnRow->addWidget(m_btnOk);
    btnRow->addWidget(m_btnCancel);
    mainLayout->addLayout(btnRow);

    connect(m_btnOk, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);

    onThreadToggled(false);
    onCounterboreToggled(false);
    onCountersinkToggled(false);
}

void HoleDialog::onThreadToggled(bool checked) {
    m_cmbThreadSpec->setEnabled(checked);
    m_edThreadClass->setEnabled(checked);
    updateThreadDepthConstraint();

    if (checked) applyTapDrillDiameter(m_cmbThreadSpec->currentText());
}

void HoleDialog::onBaseThroughToggled(bool /*throughChecked*/) {
    updateThreadDepthConstraint();
}

void HoleDialog::onBaseDepthChanged(double /*value*/) {
    updateThreadDepthConstraint();
}

void HoleDialog::onRenderModeToggled(bool checked) {
    QCheckBox *changed = qobject_cast<QCheckBox *>(sender());
    if (!changed) return;

    if (checked) {
        // 다른 두 개는 끈다 (라디오 동작)
        for (QCheckBox *cb : {m_chkRenderFull, m_chkRenderRight, m_chkRenderLeft}) {
            if (cb != changed && cb->isChecked()) {
                cb->blockSignals(true);
                cb->setChecked(false);
                cb->blockSignals(false);
            }
        }
    } else if (!m_chkRenderFull->isChecked() && !m_chkRenderRight->isChecked() &&
               !m_chkRenderLeft->isChecked()) {
        // 셋 다 꺼진 상태(마지막 하나를 방금 끈 경우)는 허용하지 않고 되돌린다
        changed->blockSignals(true);
        changed->setChecked(true);
        changed->blockSignals(false);
    }
}

// 탭은 끝에 챔퍼(불완전 나사산 구간)가 있어서 드릴 바닥까지 나사산이 파이지 않는다.
// 여유(clearance) ≈ 4 × 피치(플러그탭 챔퍼 3~5피치 근사, 규격 파싱 안 되면 지름×0.5로 대체) —
// 드릴깊이(depth) ≥ 탭깊이(threadDepth) + clearance 관계를 항상 유지.
// 탭 깊이는 기본구멍이 막힌 구멍일 때만 의미가 있음(관통이면 나사산도 전체 길이로 간주).
void HoleDialog::updateThreadDepthConstraint() {
    bool blind = m_radioBlind->isChecked();
    bool threadOn = m_chkThread->isChecked();
    bool enabled = threadOn && blind;

    m_lblThreadDepth->setEnabled(enabled);
    m_spnThreadDepth->setEnabled(enabled);

    double majorDia = 0.0, pitch = 0.0;
    double clearance = MyHole::parseThreadSpec(m_cmbThreadSpec->currentText(), majorDia, pitch)
                          ? 4.0 * pitch
                          : m_spnDiameter->value() * 0.5;

    double baseDepth = m_spnDepth->value();
    double maxThreadDepth = std::max(0.0, baseDepth - clearance);
    m_spnThreadDepth->setMaximum(baseDepth > 0.0 ? maxThreadDepth : 1000.0);
    if (enabled) m_spnThreadDepth->setValue(maxThreadDepth);
}

void HoleDialog::onThreadSpecChanged(const QString &text) {
    if (m_chkThread->isChecked()) applyTapDrillDiameter(text);
    updateThreadDepthConstraint();
}

// 탭드릴 근사식: 드릴지름 = 호칭지름(D) - 피치(P) (약 75% 나사산 결합률, ISO 미터나사 표준 탭드릴표와 거의 일치)
void HoleDialog::applyTapDrillDiameter(const QString &spec) {
    double majorDia = 0.0, pitch = 0.0;
    if (!MyHole::parseThreadSpec(spec, majorDia, pitch)) return;

    double drillDia = majorDia - pitch;
    if (drillDia > 0.0) m_spnDiameter->setValue(drillDia);
}

void HoleDialog::onCounterboreToggled(bool checked) {
    m_spnCbDiameter->setEnabled(checked);
    m_spnCbDepth->setEnabled(checked);
    if (checked) applyApproxCbCsDiameter(m_spnDiameter->value());
}

void HoleDialog::onCountersinkToggled(bool checked) {
    m_spnCsDiameter->setEnabled(checked);
    m_spnCsAngle->setEnabled(checked);
    if (checked) applyApproxCbCsDiameter(m_spnDiameter->value());
}

// 기본구멍 지름이 바뀌면: (1) 카운터보어/카운터싱크 지름이 기본구멍보다 작아지지
// 않도록 최솟값을 갱신하고, (2) 이미 적용 중이면 근사치를 다시 제안
void HoleDialog::onBaseDiameterChanged(double value) {
    m_spnCbDiameter->setMinimum(value);
    m_spnCsDiameter->setMinimum(value);
    applyApproxCbCsDiameter(value);
}

// 근사식(정밀 설계용 아님): 카운터보어/카운터싱크 지름 ≈ 기본구멍 지름 × 1.8
// 실제 볼트 규격(ISO 4762/7380/10642, ANSI B18.3 등)의 머리지름과는 오차가 있을 수 있음 —
// 여기서 채워지는 값은 시작값일 뿐, 정밀 설계 시 실제 규격표와 대조해서 수동 수정 필요.
void HoleDialog::applyApproxCbCsDiameter(double baseDiameter) {
    constexpr double kApproxHeadRatio = 1.8;
    if (baseDiameter <= 0.0) return;

    if (m_chkCounterbore->isChecked())
        m_spnCbDiameter->setValue(baseDiameter * kApproxHeadRatio);
    if (m_chkCountersink->isChecked())
        m_spnCsDiameter->setValue(baseDiameter * kApproxHeadRatio);
}

MyHole HoleDialog::resultData() const {
    MyHole d;
    d.diameter = m_spnDiameter->value();
    d.throughHole = m_radioThrough->isChecked();
    d.depth = m_spnDepth->value();

    d.isThreaded = m_chkThread->isChecked();
    d.threadSpec = m_cmbThreadSpec->currentText();
    d.threadClass = m_edThreadClass->text();
    d.threadDepth = m_spnThreadDepth->value();

    d.toleranceClass = m_edTolerance->text();

    d.hasCounterbore = m_chkCounterbore->isChecked();
    d.cbDiameter = m_spnCbDiameter->value();
    d.cbDepth = m_spnCbDepth->value();

    d.hasCountersink = m_chkCountersink->isChecked();
    d.csDiameter = m_spnCsDiameter->value();
    d.csAngle = m_spnCsAngle->value();

    d.symbolStyle = m_radioStyleGraphic->isChecked() ? HoleSymbolStyle::Graphic : HoleSymbolStyle::Text;

    d.showCenterLine = m_chkCenterLine->isChecked();

    if (m_chkRenderRight->isChecked()) d.renderMode = HoleRenderMode::Right;
    else if (m_chkRenderLeft->isChecked()) d.renderMode = HoleRenderMode::Left;
    else d.renderMode = HoleRenderMode::Full;

    d.baseLayer = m_cmbBaseLayer->currentData().toString();
    d.threadLayer = m_cmbThreadLayer->currentData().toString();
    d.cbLayer = m_cmbCbLayer->currentData().toString();
    d.csLayer = m_cmbCsLayer->currentData().toString();

    return d;
}

void HoleDialog::setData(const MyHole &data) {
    m_spnDiameter->setValue(data.diameter);
    m_radioThrough->setChecked(data.throughHole);
    m_radioBlind->setChecked(!data.throughHole);
    m_spnDepth->setValue(data.depth);
    m_spnDepth->setEnabled(!data.throughHole);

    m_chkThread->setChecked(data.isThreaded);
    m_cmbThreadSpec->setCurrentText(data.threadSpec);
    m_edThreadClass->setText(data.threadClass);
    m_spnThreadDepth->setValue(data.threadDepth);

    m_edTolerance->setText(data.toleranceClass);

    m_chkCounterbore->setChecked(data.hasCounterbore);
    m_spnCbDiameter->setValue(data.cbDiameter > 0 ? data.cbDiameter : 16.0);
    m_spnCbDepth->setValue(data.cbDepth > 0 ? data.cbDepth : 6.0);

    m_chkCountersink->setChecked(data.hasCountersink);
    m_spnCsDiameter->setValue(data.csDiameter > 0 ? data.csDiameter : 14.0);
    m_spnCsAngle->setValue(data.csAngle > 0 ? data.csAngle : 90.0);

    m_radioStyleText->setChecked(data.symbolStyle != HoleSymbolStyle::Graphic);
    m_radioStyleGraphic->setChecked(data.symbolStyle == HoleSymbolStyle::Graphic);

    m_chkCenterLine->setChecked(data.showCenterLine);

    m_chkRenderFull->blockSignals(true);
    m_chkRenderRight->blockSignals(true);
    m_chkRenderLeft->blockSignals(true);
    m_chkRenderFull->setChecked(data.renderMode == HoleRenderMode::Full);
    m_chkRenderRight->setChecked(data.renderMode == HoleRenderMode::Right);
    m_chkRenderLeft->setChecked(data.renderMode == HoleRenderMode::Left);
    m_chkRenderFull->blockSignals(false);
    m_chkRenderRight->blockSignals(false);
    m_chkRenderLeft->blockSignals(false);

    populateLayerCombo(m_cmbBaseLayer, data.baseLayer);
    populateLayerCombo(m_cmbThreadLayer, data.threadLayer);
    populateLayerCombo(m_cmbCbLayer, data.cbLayer);
    populateLayerCombo(m_cmbCsLayer, data.csLayer);

    onThreadToggled(data.isThreaded);
    onCounterboreToggled(data.hasCounterbore);
    onCountersinkToggled(data.hasCountersink);
}
