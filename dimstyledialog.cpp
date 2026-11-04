#include "dimstyledialog.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QAbstractButton>
#include <QIcon>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QVBoxLayout>

namespace {

// 화살표 스타일 미리보기 아이콘 버튼 — 별도 이미지 파일 없이 QPainter로 직접
// 그려서 보여준다(boltpickerdialog.cpp의 BoltIconButton과 동일한 아이디어).
// 체크 가능(checkable) 상태로 만들어 QButtonGroup으로 배타적 선택을 관리한다.
class ArrowStyleButton : public QAbstractButton {
public:
    ArrowStyleButton(ArrowheadStyle style, const QString &label, QWidget *parent = nullptr)
        : QAbstractButton(parent), m_style(style)
    {
        setCheckable(true);
        setCursor(Qt::PointingHandCursor);
        setToolTip(label);
    }

    ArrowheadStyle arrowStyle() const { return m_style; }

    QSize sizeHint() const override { return QSize(kSize, kSize); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);

        QRect r = rect().adjusted(1, 1, -1, -1);
        QColor bg = isChecked() ? QColor(55, 85, 130)
                                 : (underMouse() ? QColor(48, 48, 56) : QColor(36, 36, 40));
        p.fillRect(r, bg);
        p.setPen(QPen(isChecked() ? QColor(120, 170, 230) : QColor(85, 85, 90), isChecked() ? 2 : 1));
        p.drawRect(r);

        // 치수선 한 토막 + 선택된 스타일의 화살표 끝 모양 미리보기
        QRectF ir = QRectF(r).adjusted(10, 10, -10, -10);
        double midY = ir.center().y();
        double size = ir.width() * 0.45;
        QPointF tip(ir.right(), midY);
        QPointF tailEnd(tip.x() - size, midY);

        QPen linePen(Qt::white);
        linePen.setWidthF(1.3);
        p.setPen(linePen);
        p.drawLine(QPointF(ir.left(), midY), tailEnd);

        p.setPen(linePen);
        switch (m_style) {
        case ArrowheadStyle::Open: {
            QPointF s1(tailEnd.x(), tailEnd.y() - size * 0.35);
            QPointF s2(tailEnd.x(), tailEnd.y() + size * 0.35);
            p.setBrush(Qt::NoBrush);
            p.drawLine(tip, s1);
            p.drawLine(tip, s2);
            break;
        }
        case ArrowheadStyle::FilledCircle:
        case ArrowheadStyle::OpenCircle:
        case ArrowheadStyle::DoubleCircle: {
            double rad = size * 0.35;
            QPointF c = tip; // 원의 중심을 치수선 끝점(tip)에 맞춘다.
            p.setBrush(m_style == ArrowheadStyle::OpenCircle ? Qt::NoBrush : QBrush(Qt::white));
            p.drawEllipse(c, rad, rad);
            if (m_style == ArrowheadStyle::DoubleCircle) {
                p.setBrush(Qt::NoBrush);
                p.drawEllipse(c, rad * 1.7, rad * 1.7);
            }
            break;
        }
        case ArrowheadStyle::Hollow: {
            // 빈화살표: Filled와 동일한 닫힌 삼각형이지만 속을 채우지 않는다.
            QPointF s1(tailEnd.x(), tailEnd.y() - size * 0.35);
            QPointF s2(tailEnd.x(), tailEnd.y() + size * 0.35);
            QPolygonF poly;
            poly << tip << s1 << s2;
            p.setBrush(Qt::NoBrush);
            p.drawPolygon(poly);
            break;
        }
        case ArrowheadStyle::Filled:
        default: {
            QPointF s1(tailEnd.x(), tailEnd.y() - size * 0.35);
            QPointF s2(tailEnd.x(), tailEnd.y() + size * 0.35);
            QPolygonF poly;
            poly << tip << s1 << s2;
            p.setBrush(Qt::white);
            p.drawPolygon(poly);
            break;
        }
        }
        p.setBrush(Qt::NoBrush);
    }

private:
    static constexpr int kSize = 44;
    ArrowheadStyle m_style;
};

} // namespace

DimStyleDialog::DimStyleDialog(IAppContext *ctx, QWidget *parent)
    : QDialog(parent)
    , m_ctx(ctx)
{
    setWindowTitle(tr("치수선 설정 메니저"));
    setupUi();
    syncLayers();
}

void DimStyleDialog::setupUi()
{
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    // ── 1. 치수정보 ──────────────────────────────────────────────────────────
    QGroupBox   *geoGroup  = new QGroupBox(tr("치수정보"), this);
    QFormLayout *geoLayout = new QFormLayout(geoGroup);

    m_baselineSpacing = new QDoubleSpinBox(this);
    m_baselineSpacing->setRange(0, 100.0);
    m_baselineSpacing->setSingleStep(0.5);
    geoLayout->addRow(tr("치수선 간격 : "), m_baselineSpacing);

    m_extOffset = new QDoubleSpinBox(this);
    m_extOffset->setRange(0, 100.0);
    m_extOffset->setSingleStep(0.5);
    geoLayout->addRow(tr("치수보조선 간격 띄기 : "), m_extOffset);

    m_extExtend = new QDoubleSpinBox(this);
    m_extExtend->setRange(0, 100.0);
    m_extExtend->setSingleStep(0.5);
    geoLayout->addRow(tr("치수보조선 돌출 크기 : "), m_extExtend);

    mainLayout->addWidget(geoGroup);

    // ── 1-2. 화살표 ──────────────────────────────────────────────────────────
    QGroupBox   *arrowGroup  = new QGroupBox(tr("화살표"), this);
    QVBoxLayout *arrowOuter  = new QVBoxLayout(arrowGroup);

    QFormLayout *arrowSizeLayout = new QFormLayout();
    m_arrowSize = new QDoubleSpinBox(this);
    m_arrowSize->setRange(0.1, 100.0);
    m_arrowSize->setSingleStep(0.5);
    arrowSizeLayout->addRow(tr("화살표 크기 : "), m_arrowSize);
    arrowOuter->addLayout(arrowSizeLayout);

    QHBoxLayout *arrowIconRow = new QHBoxLayout();
    arrowIconRow->setSpacing(10);

    struct ArrowEntry { ArrowheadStyle style; QString label; };
    const ArrowEntry arrowEntries[6] = {
        { ArrowheadStyle::Filled,       tr("채운화살표") },
        { ArrowheadStyle::Hollow,       tr("빈화살표") },
        { ArrowheadStyle::Open,         tr("열린화살표") },
        { ArrowheadStyle::FilledCircle, tr("채운원") },
        { ArrowheadStyle::OpenCircle,   tr("빈원") },
        { ArrowheadStyle::DoubleCircle, tr("이중원") },
    };

    m_arrowStyleGroup = new QButtonGroup(this);
    m_arrowStyleGroup->setExclusive(true);

    for (const ArrowEntry &entry : arrowEntries) {
        ArrowStyleButton *btn = new ArrowStyleButton(entry.style, entry.label, arrowGroup);
        m_arrowStyleGroup->addButton(btn, static_cast<int>(entry.style));

        QLabel *caption = new QLabel(entry.label, arrowGroup);
        caption->setAlignment(Qt::AlignHCenter | Qt::AlignTop);

        QVBoxLayout *itemLayout = new QVBoxLayout();
        itemLayout->setSpacing(4);
        itemLayout->addWidget(btn, 0, Qt::AlignHCenter);
        itemLayout->addWidget(caption, 0, Qt::AlignHCenter);

        arrowIconRow->addLayout(itemLayout);
    }
    arrowIconRow->addStretch();

    arrowOuter->addLayout(arrowIconRow);
    m_arrowStyleGroup->button(static_cast<int>(ArrowheadStyle::Filled))->setChecked(true);

    mainLayout->addWidget(arrowGroup);

    // ── 2. 글자 정보 ─────────────────────────────────────────────────────────
    QGroupBox   *textGroup  = new QGroupBox(tr("글자 정보"), this);
    QFormLayout *textLayout = new QFormLayout(textGroup);

    m_textSize = new QDoubleSpinBox(this);
    m_textSize->setRange(0.1, 100.0);
    m_textSize->setSingleStep(0.5);
    textLayout->addRow(tr("글자 크기 : "), m_textSize);

    m_useThousandSep = new QCheckBox(tr("자릿점 넣기 (1,000 단위 구분)"), this);
    textLayout->addRow("", m_useThousandSep);

    m_toleranceSize = new QDoubleSpinBox(this);
    m_toleranceSize->setRange(0.1, 100.0);
    m_toleranceSize->setSingleStep(0.5);
    textLayout->addRow(tr("공차 글자 크기 : "), m_toleranceSize);

    m_textGap = new QDoubleSpinBox(this);
    m_textGap->setRange(0, 100.0);
    m_textGap->setSingleStep(0.5);
    textLayout->addRow(tr("치수선 글자 띄기 : "), m_textGap);

    m_precision = new QSpinBox(this);
    m_precision->setRange(0, 8);
    textLayout->addRow(tr("주치수 소수점 (precision):"), m_precision);

    m_tolPrecision = new QSpinBox(this);
    m_tolPrecision->setRange(0, 8);
    textLayout->addRow(tr("공차 소수점 (tol precision):"), m_tolPrecision);

    m_suppressZero = new QCheckBox(tr("주치수 제로 억제 (trailing zeros)"), this);
    textLayout->addRow("", m_suppressZero);

    m_suppressTolZero = new QCheckBox(tr("공차 제로 억제 (tolerance trailing zeros)"), this);
    textLayout->addRow("", m_suppressTolZero);

    mainLayout->addWidget(textGroup);

    // ── 3. 치수선 스케일 (DrawingSettingsDialog의 도면 스케일과 동일 방식) ──
    QGroupBox    *scaleGroup  = new QGroupBox(tr("치수선 스케일"), this);
    QHBoxLayout  *scaleLayout = new QHBoxLayout(scaleGroup);

    scaleLayout->addWidget(new QLabel(tr("1 / "), scaleGroup));

    m_dimScaleEdit = new QLineEdit("1", scaleGroup);
    scaleLayout->addWidget(m_dimScaleEdit);

    m_comboDimScale = new QComboBox(scaleGroup);
    m_comboDimScale->addItems({"Custom", "10", "20", "25", "50", "75", "100", "150", "200"});
    scaleLayout->addWidget(m_comboDimScale);

    m_btnPickDimScale = new QPushButton(tr("Pick"), scaleGroup);
    m_btnPickDimScale->setFixedWidth(60);
    scaleLayout->addWidget(m_btnPickDimScale);

    mainLayout->addWidget(scaleGroup);

    // Pick 버튼 → 시그널 전달
    connect(m_btnPickDimScale, &QPushButton::clicked,
            this, &DimStyleDialog::pickDimScaleRequested);

    // 프리셋 콤보박스 →  스케일 편집창 동기화
    connect(m_comboDimScale, QOverload<int>::of(&QComboBox::activated),
            this, &DimStyleDialog::onDimScalePresetChanged);

    // ── 4. 레이어 정보 ───────────────────────────────────────────────────────
    QGroupBox   *layerGroup  = new QGroupBox(tr("레이어 정보"), this);
    QFormLayout *layerLayout = new QFormLayout(layerGroup);

    m_dimLineLayer = new QComboBox(this);
    layerLayout->addRow(tr("치수선 레이어 : "), m_dimLineLayer);

    m_dimTextLayer = new QComboBox(this);
    layerLayout->addRow(tr("치수선 글자 레이어 : "), m_dimTextLayer);

    mainLayout->addWidget(layerGroup);

    // ── OK / Cancel ──────────────────────────────────────────────────────────
    QDialogButtonBox *buttonBox =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    mainLayout->addWidget(buttonBox);
}

void DimStyleDialog::syncLayers()
{
    m_dimLineLayer->clear();
    m_dimTextLayer->clear();

    m_dimLineLayer->addItem(tr(" 현재 레이어 "), "");
    m_dimTextLayer->addItem(tr(" 현재 레이어 "), "");

    if (m_ctx && m_ctx->layerManager()) {
        const auto &layers = m_ctx->layerManager()->getAllLayers();
        for (int i = 0; i < layers.size(); ++i) {
            QPixmap pix(20, 20);
            pix.fill(layers[i].color());
            QIcon icon(pix);

            m_dimLineLayer->addItem(icon, layers[i].name(), layers[i].name());
            m_dimTextLayer->addItem(icon, layers[i].name(), layers[i].name());
        }
    }
}

// ── 치수선 스케일 슬롯 ───────────────────────────────────────────────────────
void DimStyleDialog::onDimScalePresetChanged(int index)
{
    if (index > 0) { // "Custom" 제외
        m_dimScaleEdit->setText(m_comboDimScale->currentText());
    }
}

void DimStyleDialog::setDimScale(double scale)
{
    m_dimScaleEdit->setText(QString::number(scale, 'f', 2));

    int val  = qRound(scale);
    int idx  = m_comboDimScale->findText(QString::number(val));
    if (idx != -1 && std::abs(scale - val) < 0.01) {
        m_comboDimScale->setCurrentIndex(idx);
    } else {
        m_comboDimScale->setCurrentIndex(0); // "Custom"
    }
}

// ── getStyle / setStyle ──────────────────────────────────────────────────────
DimensionStyle DimStyleDialog::getStyle() const
{
    DimensionStyle s;
    s.arrowSize       = m_arrowSize->value();
    int checkedId = m_arrowStyleGroup->checkedId();
    s.arrowStyle = (checkedId >= 0) ? static_cast<ArrowheadStyle>(checkedId) : ArrowheadStyle::Filled;
    s.baselineSpacing = m_baselineSpacing->value();
    s.textSize        = m_textSize->value();
    s.toleranceSize   = m_toleranceSize->value();
    s.textGap         = m_textGap->value();
    s.extOffset       = m_extOffset->value();
    s.extExtend       = m_extExtend->value();
    s.precision       = m_precision->value();
    s.tolPrecision    = m_tolPrecision->value();
    s.suppressTrailingZeros = m_suppressZero->isChecked();
    s.suppressTrailingZerosTol = m_suppressTolZero->isChecked();
    s.useThousandSep = m_useThousandSep->isChecked();
    s.dimLineLayer    = m_dimLineLayer->currentData().toString();
    s.dimTextLayer    = m_dimTextLayer->currentData().toString();

    bool ok;
    double sc = m_dimScaleEdit->text().toDouble(&ok);
    s.dimScale = (ok && sc > 0.0) ? sc : 1.0;

    return s;
}

void DimStyleDialog::setStyle(const DimensionStyle &style)
{
    m_arrowSize->setValue(style.arrowSize);
    QAbstractButton *arrowBtn = m_arrowStyleGroup->button(static_cast<int>(style.arrowStyle));
    if (arrowBtn) arrowBtn->setChecked(true);
    m_baselineSpacing->setValue(style.baselineSpacing);
    m_textSize->setValue(style.textSize);
    m_toleranceSize->setValue(style.toleranceSize);
    m_textGap->setValue(style.textGap);
    m_extOffset->setValue(style.extOffset);
    m_extExtend->setValue(style.extExtend);
    m_precision->setValue(style.precision);
    m_tolPrecision->setValue(style.tolPrecision);
    m_suppressZero->setChecked(style.suppressTrailingZeros);
    m_suppressTolZero->setChecked(style.suppressTrailingZerosTol);
    m_useThousandSep->setChecked(style.useThousandSep);

    setDimScale(style.dimScale);

    int idxLine = m_dimLineLayer->findData(style.dimLineLayer);
    if (idxLine >= 0)
        m_dimLineLayer->setCurrentIndex(idxLine);

    int idxText = m_dimTextLayer->findData(style.dimTextLayer);
    if (idxText >= 0)
        m_dimTextLayer->setCurrentIndex(idxText);
}
