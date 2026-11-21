#include "bearingpickerdialog.h"

#include "layermanager.h"
#include "mygraphicsscene.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QPixmap>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QTransform>
#include <QVBoxLayout>
#include <algorithm>

namespace {
const int kIconSize = 130;
}

BearingIconButton::BearingIconButton(const QString &label, const QPainterPath &previewPath,
                                     BearingKind kind, QWidget *parent)
    : QAbstractButton(parent), m_previewPath(previewPath), m_kind(kind)
{
    setCursor(Qt::PointingHandCursor);
    setToolTip(label);
}

QSize BearingIconButton::sizeHint() const {
    return QSize(kIconSize, kIconSize);
}

void BearingIconButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);
    QColor bg = isDown() ? QColor(55, 85, 130) : (underMouse() ? QColor(48, 48, 56) : QColor(36, 36, 40));
    p.fillRect(r, bg);
    p.setPen(QPen(underMouse() ? QColor(120, 170, 230) : QColor(85, 85, 90), 1));
    p.drawRect(r);

    QRect iconRect = r.adjusted(10, 10, -10, -10);
    if (iconRect.width() > 10 && iconRect.height() > 10) {
        QRectF bounds = m_previewPath.boundingRect();
        if (bounds.width() > 1e-6 && bounds.height() > 1e-6) {
            double scale = std::min(iconRect.width() / bounds.width(), iconRect.height() / bounds.height());
            QTransform t;
            t.translate(iconRect.center().x(), iconRect.center().y());
            t.scale(scale, scale);
            t.translate(-bounds.center().x(), -bounds.center().y());

            p.save();
            p.setTransform(t);
            QPen pen(Qt::white);
            pen.setCosmetic(true);
            pen.setWidthF(0.25);
            p.setPen(pen);
            p.drawPath(m_previewPath);
            p.restore();
        }
    }
}

BearingPickerDialog::BearingPickerDialog(IAppContext *ctx, QWidget *parent)
    : QDialog(parent), m_selectedKind(BearingKind::DeepGroove), m_ctx(ctx)
{
    setWindowTitle(tr("베어링 종류 선택"));
    setupUI();
}

QPainterPath BearingPickerDialog::previewPathFor(BearingKind kind) {
    const QPointF origin(0.0, 0.0);
    switch (kind) {
    case BearingKind::DeepGroove: {
        DeepGrooveBearingParams p;
        p.drawDims = false;
        p.drawCenter = false;
        return DeepGrooveBearingTool::buildPath(p, origin);
    }
    case BearingKind::TaperedRoller: {
        TaperedRollerBearingParams p;
        p.drawDims = false;
        p.drawCenter = false;
        return TaperedRollerBearingTool::buildPath(p, origin);
    }
    case BearingKind::Thrust: {
        ThrustBearingParams p;
        p.drawDims = false;
        p.drawCenter = false;
        return ThrustBearingTool::buildPath(p, origin);
    }
    }
    return QPainterPath();
}

void BearingPickerDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    QHBoxLayout *row = new QHBoxLayout;
    row->setSpacing(14);

    struct Entry { BearingKind kind; QString label; };
    static const Entry kEntries[3] = {
        { BearingKind::DeepGroove,    tr("깊은홈볼베어링") },
        { BearingKind::TaperedRoller, tr("테이퍼롤러베어링") },
        { BearingKind::Thrust,        tr("스러스트베어링") },
    };

    for (int i = 0; i < 3; ++i) {
        BearingIconButton *btn = new BearingIconButton(kEntries[i].label, previewPathFor(kEntries[i].kind),
                                                       kEntries[i].kind, this);
        connect(btn, &QAbstractButton::clicked, this, &BearingPickerDialog::onIconClicked);

        QLabel *nameLabel = new QLabel(kEntries[i].label, this);
        nameLabel->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        nameLabel->setWordWrap(true);
        nameLabel->setFixedWidth(kIconSize);

        QVBoxLayout *itemLayout = new QVBoxLayout;
        itemLayout->setSpacing(6);
        itemLayout->addWidget(btn, 0, Qt::AlignHCenter);
        itemLayout->addWidget(nameLabel, 0, Qt::AlignHCenter);
        row->addLayout(itemLayout);
    }
    mainLayout->addLayout(row);

    // 베어링 레이어 : 외형 도형(와셔·볼·롤러 등)이 그려질 레이어
    QHBoxLayout *layerRow = new QHBoxLayout;
    layerRow->addWidget(new QLabel(tr("베어링 레이어 :"), this));
    m_layerCombo = new QComboBox(this);
    m_layerCombo->setMinimumWidth(180);
    const QString saved = BearingLayer::bodyLayer(m_ctx ? m_ctx->activeScene() : nullptr);
    QStringList names;
    QList<QColor> colors;
    if (m_ctx && m_ctx->layerManager()) {
        const QList<Layer> layers = m_ctx->layerManager()->getAllLayers();
        for (int i = 0; i < layers.size(); ++i) {
            names << layers.at(i).name();
            colors << layers.at(i).color();
        }
    }
    if (!names.contains(saved)) {   // 아직 없는 레이어(BEARING)는 그릴 때 자동 생성(흰색)
        names.prepend(saved);
        colors.prepend(QColor(255, 255, 255));
    }
    for (int i = 0; i < names.size(); ++i) {
        QPixmap pm(14, 14);
        pm.fill(colors.at(i));
        QPainter pp(&pm);
        pp.setPen(QColor(120, 120, 120));
        pp.drawRect(0, 0, 13, 13);
        pp.end();
        m_layerCombo->addItem(QIcon(pm), names.at(i));
    }
    m_layerCombo->setCurrentText(saved);
    layerRow->addWidget(m_layerCombo, 1);
    mainLayout->addLayout(layerRow);

    QPushButton *cancelBtn = new QPushButton(tr("취소"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    QHBoxLayout *btnLay = new QHBoxLayout;
    btnLay->addStretch();
    btnLay->addWidget(cancelBtn);
    mainLayout->addLayout(btnLay);
}

QString BearingPickerDialog::bearingLayer() const {
    return m_layerCombo ? m_layerCombo->currentText() : BearingLayer::defaultName();
}

void BearingPickerDialog::onIconClicked() {
    BearingIconButton *btn = qobject_cast<BearingIconButton *>(sender());
    if (!btn) return;
    m_selectedKind = btn->kind();
    accept();
}
