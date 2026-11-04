#include "hhboltsetupdialog.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "layer.h"

#include <QComboBox>
#include <QLabel>
#include <QCheckBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>
#include <QPixmap>

// ─────────────────────────────────────────────
// HHBoltSetupDialog 구현
// ─────────────────────────────────────────────
HHBoltSetupDialog::HHBoltSetupDialog(IAppContext *ctx, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("접시머리렌치볼트 설정"));
    setFixedWidth(340);
    setupUI(ctx);
}

void HHBoltSetupDialog::setupUI(IAppContext *ctx) {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QFormLayout *formLayout = new QFormLayout;

    // 규격: 현재는 ISO 10642(접시머리 육각구멍붙이볼트)만 지원하므로 고정 라벨로만 표시한다.
    formLayout->addRow(tr("규격 :"),
                       new QLabel(tr("ISO 10642 (접시머리렌치볼트)"), this));

    // 호칭경 콤보박스: mechpartstandards.h 의 접시머리렌치볼트 규격표에서 채운다.
    m_comboDiameter = new QComboBox(this);
    for (const HHBoltSize &sz : kKsHHBolts()) {
        m_comboDiameter->addItem(sz.label);
    }
    formLayout->addRow(tr("호칭경(d) :"), m_comboDiameter);

    // 길이 콤보박스: KS 표준 호칭길이 계열에서 선택 (자유 입력 금지, 머리 상단 기준)
    // 옆에 "2점 지정" 체크박스 — 켜면 배치 시 클릭한 두 점 사이 거리가 길이(L)가 되고
    // 콤보박스 값은 무시된다(그래서 켜지면 콤보박스를 비활성화해 시각적으로 알려준다).
    m_comboLength = new QComboBox(this);
    for (int len : kKsPreferredBoltLengths()) {
        m_comboLength->addItem(QString::fromUtf8("%1 mm").arg(len), len);
    }
    m_chkTwoPoint = new QCheckBox(tr("2점 지정"), this);
    connect(m_chkTwoPoint, &QCheckBox::toggled, this, &HHBoltSetupDialog::onTwoPointModeToggled);
    QHBoxLayout *lengthLay = new QHBoxLayout;
    lengthLay->setContentsMargins(0, 0, 0, 0);
    lengthLay->addWidget(m_comboLength);
    lengthLay->addWidget(m_chkTwoPoint);
    formLayout->addRow(tr("길이(L) :"), lengthLay);

    // 레이어 콤보박스 2개: 외형선 / 골지름선을 서로 다른 레이어에 넣을 수 있게 분리
    m_comboOutlineLayer = new QComboBox(this);
    populateLayerCombo(ctx, m_comboOutlineLayer);
    formLayout->addRow(tr("외형선 레이어 :"), m_comboOutlineLayer);

    m_comboThreadLayer = new QComboBox(this);
    populateLayerCombo(ctx, m_comboThreadLayer);
    formLayout->addRow(tr("골지름선 레이어 :"), m_comboThreadLayer);

    mainLayout->addLayout(formLayout);

    // 골지름선 레이어 바로 아래: 전나사/부분나사 선택 (기본 = 전나사) + 중심선/평면도 표시 여부 (기본 = 표시)
    m_chkFullThread = new QCheckBox(tr("전나사"), this);
    m_chkFullThread->setChecked(true);
    m_chkCenterline = new QCheckBox(tr("중심선"), this);
    m_chkCenterline->setChecked(true);
    m_chkSideView = new QCheckBox(tr("측면"), this);
    m_chkSideView->setChecked(true);
    QHBoxLayout *threadLay = new QHBoxLayout;
    threadLay->setContentsMargins(0, 0, 0, 0);
    threadLay->addWidget(m_chkFullThread);
    threadLay->addWidget(m_chkCenterline);
    threadLay->addWidget(m_chkSideView);
    threadLay->addStretch();
    mainLayout->addLayout(threadLay);

    QDialogButtonBox *btnBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(btnBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(btnBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(btnBox);
}

void HHBoltSetupDialog::populateLayerCombo(IAppContext *ctx, QComboBox *combo) {
    combo->clear();
    combo->addItem(tr(" 현재 레이어 "), "");
    if (!ctx || !ctx->layerManager()) return;

    const auto &layers = ctx->layerManager()->getAllLayers();
    for (int i = 0; i < layers.size(); ++i) {
        QPixmap pixmap(14, 14);
        pixmap.fill(layers[i].color());
        combo->addItem(QIcon(pixmap), layers[i].name(), layers[i].name());
    }
}

HHBoltSize HHBoltSetupDialog::selectedSize() const {
    int idx = m_comboDiameter->currentIndex();
    if (idx >= 0 && idx < kKsHHBolts().size()) {
        return kKsHHBolts()[idx];
    }
    return kKsHHBolts().isEmpty() ? HHBoltSize() : kKsHHBolts().first();
}

double HHBoltSetupDialog::selectedLength() const {
    return m_comboLength->currentData().toDouble();
}

QString HHBoltSetupDialog::selectedOutlineLayer() const {
    return m_comboOutlineLayer->currentData().toString();
}

QString HHBoltSetupDialog::selectedThreadLayer() const {
    return m_comboThreadLayer->currentData().toString();
}

bool HHBoltSetupDialog::twoPointMode() const {
    return m_chkTwoPoint->isChecked();
}

bool HHBoltSetupDialog::fullThread() const {
    return m_chkFullThread->isChecked();
}

bool HHBoltSetupDialog::centerlineEnabled() const {
    return m_chkCenterline->isChecked();
}

bool HHBoltSetupDialog::sideViewEnabled() const {
    return m_chkSideView->isChecked();
}

void HHBoltSetupDialog::onTwoPointModeToggled(bool checked) {
    // 2점 지정 모드에서는 콤보박스 길이값이 쓰이지 않으므로 비활성화해 알려준다.
    m_comboLength->setEnabled(!checked);
}

void HHBoltSetupDialog::setInitialSelection(const QString &diameterLabel, double length,
                                            const QString &outlineLayer, const QString &threadLayer,
                                            bool twoPointMode, bool fullThread,
                                            bool centerlineEnabled, bool sideViewEnabled) {
    int dIdx = m_comboDiameter->findText(diameterLabel);
    if (dIdx >= 0) m_comboDiameter->setCurrentIndex(dIdx);

    int lIdx = m_comboLength->findData((int)length);
    if (lIdx >= 0) m_comboLength->setCurrentIndex(lIdx);

    int outlineIdx = m_comboOutlineLayer->findData(outlineLayer);
    m_comboOutlineLayer->setCurrentIndex(outlineIdx >= 0 ? outlineIdx : 0);

    int threadIdx = m_comboThreadLayer->findData(threadLayer);
    m_comboThreadLayer->setCurrentIndex(threadIdx >= 0 ? threadIdx : 0);

    m_chkTwoPoint->setChecked(twoPointMode); // onTwoPointModeToggled가 콤보박스 활성상태도 같이 맞춰줌
    m_chkFullThread->setChecked(fullThread);
    m_chkCenterline->setChecked(centerlineEnabled);
    m_chkSideView->setChecked(sideViewEnabled);
}
