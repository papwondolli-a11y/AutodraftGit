#include "surfacefinishdialog.h"
#include "surfacefinishitem.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "layer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QDialogButtonBox>
#include <QPainter>
#include <QStyleOptionGraphicsItem>
#include <QRadioButton>
#include <QPixmap>

// ─────────────────────────────────────────────
// SurfaceFinishDialog 구현
// ─────────────────────────────────────────────
static const struct { SurfaceFinishSymbol sym; const char *tip; } kIsoSymbols[] = {
    { SurfaceFinishSymbol::Basic,               QT_TRANSLATE_NOOP("SurfaceFinishDialog", "기본 (가공방법 불문)") },
    { SurfaceFinishSymbol::TriangleFlag,        QT_TRANSLATE_NOOP("SurfaceFinishDialog", "삼각 기호 (특수 표시)") },
    { SurfaceFinishSymbol::MachiningProhibited, QT_TRANSLATE_NOOP("SurfaceFinishDialog", "제거가공 금지 (주조·단조면 그대로 유지)") },
};
static const int kIsoCount = (int)(sizeof(kIsoSymbols) / sizeof(kIsoSymbols[0]));

static const struct { int grade; const char *label; } kLegacyGrades[] = {
    { 1, QT_TRANSLATE_NOOP("SurfaceFinishDialog", "거친다듬질\n(▽)") },
    { 2, QT_TRANSLATE_NOOP("SurfaceFinishDialog", "중다듬질\n(▽▽)") },
    { 3, QT_TRANSLATE_NOOP("SurfaceFinishDialog", "상다듬질\n(▽▽▽)") },
    { 4, QT_TRANSLATE_NOOP("SurfaceFinishDialog", "정밀다듬질\n(▽▽▽▽)") },
};
static const int kLegacyCount = (int)(sizeof(kLegacyGrades) / sizeof(kLegacyGrades[0]));

SurfaceFinishDialog::SurfaceFinishDialog(IAppContext *ctx, QWidget *parent)
    : QDialog(parent), m_ctx(ctx)
{
    setWindowTitle(tr("표면 거칠기"));
    setFixedWidth(420);
    setupUI();
}

void SurfaceFinishDialog::populateLayerCombo(QComboBox *combo) {
    combo->clear();
    combo->addItem(tr(" 현재 레이어 "), "");
    if (!m_ctx || !m_ctx->layerManager()) return;

    const auto &layers = m_ctx->layerManager()->getAllLayers();
    for (int i = 0; i < layers.size(); ++i) {
        QPixmap pixmap(14, 14);
        pixmap.fill(layers[i].color());
        combo->addItem(QIcon(pixmap), layers[i].name(), layers[i].name());
    }
}

void SurfaceFinishDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // ── 표기 방식 선택 ──
    QGroupBox *modeBox = new QGroupBox(tr("표기 방식"), this);
    QHBoxLayout *modeLay = new QHBoxLayout(modeBox);
    m_modeGroup = new QButtonGroup(this);
    QRadioButton *rbIso = new QRadioButton(tr("신기호 (ISO / 현행 KS)"), this);
    QRadioButton *rbLegacy = new QRadioButton(tr("구기호 (다듬질 등급)"), this);
    rbIso->setChecked(true);
    m_modeGroup->addButton(rbIso, 0);
    m_modeGroup->addButton(rbLegacy, 1);
    modeLay->addWidget(rbIso);
    modeLay->addWidget(rbLegacy);
    mainLayout->addWidget(modeBox);
    connect(m_modeGroup, &QButtonGroup::idClicked, this, &SurfaceFinishDialog::onModeChanged);

    m_stack = new QStackedWidget(this);

    // ── 페이지 0: 신기호(ISO) ──
    QWidget *isoPage = new QWidget(this);
    QVBoxLayout *isoLay = new QVBoxLayout(isoPage);
    isoLay->setContentsMargins(0, 0, 0, 0);

    QGroupBox *symBox = new QGroupBox(tr("기호 종류"), isoPage);
    QVBoxLayout *symOuterLay = new QVBoxLayout(symBox);
    QHBoxLayout *symLay = new QHBoxLayout;
    m_isoSymGroup = new QButtonGroup(this);
    const QSize iconSize(72, 72);
    for (int i = 0; i < kIsoCount; ++i) {
        QPushButton *btn = new QPushButton(this);
        btn->setToolTip(tr(kIsoSymbols[i].tip));
        btn->setCheckable(true);
        btn->setIcon(renderSymbolIcon(kIsoSymbols[i].sym, iconSize));
        btn->setIconSize(iconSize);
        btn->setFixedSize(iconSize.width() + 16, iconSize.height() + 16);
        btn->setStyleSheet("QPushButton:checked { background-color: #0078d4; }");
        if (kIsoSymbols[i].sym == m_isoSymbol) btn->setChecked(true);
        m_isoSymGroup->addButton(btn, i);
        symLay->addWidget(btn);
    }
    connect(m_isoSymGroup, &QButtonGroup::idClicked, this, &SurfaceFinishDialog::onIsoSymbolSelected);
    symOuterLay->addLayout(symLay);

    m_chkTailBar = new QCheckBox(tr("꼬리선 (긴 다리 끝에 막대 추가 — 제거가공 필요 표시)"), symBox);
    symOuterLay->addWidget(m_chkTailBar);

    isoLay->addWidget(symBox);

    QGroupBox *valBox = new QGroupBox(tr("값 (Ra)"), isoPage);
    QVBoxLayout *valOuterLay = new QVBoxLayout(valBox);

    QHBoxLayout *raModeLay = new QHBoxLayout;
    m_raModeGroup = new QButtonGroup(this);
    static const char *raModeLabels[5] = {"Ra", "W", "X", "Y", "Z"};
    for (int i = 0; i < 5; ++i) {
        QRadioButton *rb = new QRadioButton(QString::fromUtf8(raModeLabels[i]), this);
        if (i == 0) rb->setChecked(true); // 기본 활성화: RA
        m_raModeGroup->addButton(rb, i);
        raModeLay->addWidget(rb);
    }
    connect(m_raModeGroup, &QButtonGroup::idClicked, this, &SurfaceFinishDialog::onRaModeChanged);
    valOuterLay->addLayout(raModeLay);

    QGridLayout *valLay = new QGridLayout;
    valLay->addWidget(new QLabel(tr("Ra 값:"), this), 0, 0);
    m_edRa = new QComboBox(this);
    m_edRa->setEditable(true);
    m_edRa->addItems({"12.5", "3.2", "0.8", "0.2"});
    m_edRa->setCurrentIndex(-1);
    m_edRa->lineEdit()->setPlaceholderText(tr("예: 3.2 (목록 선택 또는 직접 입력)"));
    valLay->addWidget(m_edRa, 0, 1);
    valLay->addWidget(new QLabel(tr("가공방법(선택):"), this), 1, 0);
    m_edMethod = new QLineEdit(this);
    m_edMethod->setPlaceholderText(tr("예: 밀링"));
    valLay->addWidget(m_edMethod, 1, 1);
    valOuterLay->addLayout(valLay);

    isoLay->addWidget(valBox);
    m_stack->addWidget(isoPage);

    // ── 페이지 1: 구기호(다듬질 등급) ──
    QWidget *legacyPage = new QWidget(this);
    QVBoxLayout *legacyLay = new QVBoxLayout(legacyPage);
    legacyLay->setContentsMargins(0, 0, 0, 0);
    QGroupBox *gradeBox = new QGroupBox(tr("다듬질 등급"), legacyPage);
    QGridLayout *gradeLay = new QGridLayout(gradeBox);
    m_legacyGroup = new QButtonGroup(this);
    for (int i = 0; i < kLegacyCount; ++i) {
        QPushButton *btn = new QPushButton(tr(kLegacyGrades[i].label), this);
        btn->setCheckable(true);
        btn->setMinimumHeight(44);
        btn->setStyleSheet("QPushButton:checked { background-color: #0078d4; color: white; }");
        if (kLegacyGrades[i].grade == m_legacyGrade) btn->setChecked(true);
        m_legacyGroup->addButton(btn, kLegacyGrades[i].grade);
        gradeLay->addWidget(btn, i / 2, i % 2);
    }
    connect(m_legacyGroup, &QButtonGroup::idClicked, this, &SurfaceFinishDialog::onLegacyGradeSelected);
    legacyLay->addWidget(gradeBox);
    m_stack->addWidget(legacyPage);

    mainLayout->addWidget(m_stack);

    // ── 문자 높이 / 지시선 옵션 ──
    QHBoxLayout *hLay = new QHBoxLayout;
    hLay->addWidget(new QLabel(tr("문자 높이:"), this));
    m_spnTextHeight = new QDoubleSpinBox(this);
    m_spnTextHeight->setRange(1.0, 50.0);
    m_spnTextHeight->setSingleStep(0.5);
    m_spnTextHeight->setValue(2.5);
    m_spnTextHeight->setSuffix(" mm");
    hLay->addWidget(m_spnTextHeight);
    m_chkUseLeader = new QCheckBox(tr("지시선"), this);
    m_chkUseLeader->setChecked(true);
    hLay->addWidget(m_chkUseLeader);
    hLay->addStretch();
    mainLayout->addLayout(hLay);

    // ── 레이어 (기호/문자 색상·굵기를 별도 레이어에서 가져옴) ──
    QGroupBox *layerBox = new QGroupBox(tr("레이어"), this);
    QFormLayout *layerLay = new QFormLayout(layerBox);
    m_comboSymbolLayer = new QComboBox(this);
    populateLayerCombo(m_comboSymbolLayer);
    layerLay->addRow(tr("기호레이어 :"), m_comboSymbolLayer);
    m_comboTextLayer = new QComboBox(this);
    populateLayerCombo(m_comboTextLayer);
    layerLay->addRow(tr("문자레이어 :"), m_comboTextLayer);
    mainLayout->addWidget(layerBox);

    QDialogButtonBox *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btnBox);
}

void SurfaceFinishDialog::onModeChanged() {
    m_legacyMode = (m_modeGroup->checkedId() == 1);
    m_stack->setCurrentIndex(m_legacyMode ? 1 : 0);
}

void SurfaceFinishDialog::onIsoSymbolSelected(int id) {
    if (id >= 0 && id < kIsoCount) {
        m_isoSymbol = kIsoSymbols[id].sym;
    }
}

void SurfaceFinishDialog::onLegacyGradeSelected(int id) {
    m_legacyGrade = qBound(1, id, 4);
}

void SurfaceFinishDialog::onRaModeChanged(int id) {
    static const char *raModeLabels[5] = {"", "W", "X", "Y", "Z"};
    m_raLetterCode = (id >= 1 && id <= 4) ? QString::fromUtf8(raModeLabels[id]) : QString();
    // RA(숫자값) 모드가 아니면 Ra 값 콤보박스는 필요 없으므로 비활성화
    m_edRa->setEnabled(m_raLetterCode.isEmpty());
}

QIcon SurfaceFinishDialog::renderSymbolIcon(SurfaceFinishSymbol sym, const QSize &size) {
    QPixmap pixmap(size);
    pixmap.fill(Qt::transparent);

    SurfaceFinishData d;
    d.symbol = sym;
    d.useLeader = false;
    d.leaderPoints.clear();
    d.textHeight = size.height() * 0.22;
    d.position = MyVector(size.width() * 0.30, size.height() * 0.24);

    SurfaceFinishItem item(d);
    item.setOverrideScale(1.0);
    item.setPen(QPen(QColor(220, 220, 220), 1.6));

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // 씬 좌표계(Y가 위로 증가하는 CAD 관례)와 동일하게 좌하단 원점으로 반전
    painter.translate(0, size.height());
    painter.scale(1, -1);

    QStyleOptionGraphicsItem opt;
    item.paint(&painter, &opt, nullptr);
    painter.end();

    return QIcon(pixmap);
}

SurfaceFinishData SurfaceFinishDialog::resultData() const {
    SurfaceFinishData d;
    d.symbol = m_legacyMode ? SurfaceFinishSymbol::LegacyGrade : m_isoSymbol;
    d.raValue = m_edRa->currentText().trimmed();
    d.raLetterCode = m_raLetterCode;
    d.productionMethod = m_edMethod->text().trimmed();
    d.legacyGrade = m_legacyGrade;
    d.showTailBar = m_chkTailBar->isChecked();
    d.textHeight = m_spnTextHeight->value();
    d.useLeader = m_chkUseLeader->isChecked();
    d.symbolLayer = m_comboSymbolLayer->currentData().toString();
    d.textLayer = m_comboTextLayer->currentData().toString();
    return d;
}

void SurfaceFinishDialog::setData(const SurfaceFinishData &data) {
    m_legacyMode = (data.symbol == SurfaceFinishSymbol::LegacyGrade);
    m_modeGroup->button(m_legacyMode ? 1 : 0)->setChecked(true);
    m_stack->setCurrentIndex(m_legacyMode ? 1 : 0);

    m_isoSymbol = m_legacyMode ? SurfaceFinishSymbol::Basic : data.symbol;
    for (int i = 0; i < kIsoCount; ++i) {
        if (kIsoSymbols[i].sym == m_isoSymbol) {
            QAbstractButton *btn = m_isoSymGroup->button(i);
            if (btn) btn->setChecked(true);
            break;
        }
    }

    m_legacyGrade = qBound(1, data.legacyGrade, 4);
    QAbstractButton *gbtn = m_legacyGroup->button(m_legacyGrade);
    if (gbtn) gbtn->setChecked(true);

    m_edRa->setCurrentText(data.raValue);
    m_edMethod->setText(data.productionMethod);
    m_chkTailBar->setChecked(data.showTailBar);
    m_spnTextHeight->setValue(data.textHeight);
    m_chkUseLeader->setChecked(data.useLeader);

    m_raLetterCode = data.raLetterCode;
    static const char *raModeLabels[5] = {"", "W", "X", "Y", "Z"};
    int raModeId = 0;
    for (int i = 1; i < 5; ++i) {
        if (m_raLetterCode == QString::fromUtf8(raModeLabels[i])) { raModeId = i; break; }
    }
    QAbstractButton *raBtn = m_raModeGroup->button(raModeId);
    if (raBtn) raBtn->setChecked(true);
    m_edRa->setEnabled(m_raLetterCode.isEmpty());

    int symIdx = m_comboSymbolLayer->findData(data.symbolLayer);
    m_comboSymbolLayer->setCurrentIndex(symIdx >= 0 ? symIdx : 0);
    int txtIdx = m_comboTextLayer->findData(data.textLayer);
    m_comboTextLayer->setCurrentIndex(txtIdx >= 0 ? txtIdx : 0);
}
