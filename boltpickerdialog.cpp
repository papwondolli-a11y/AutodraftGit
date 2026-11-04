#include "boltpickerdialog.h"
#include "mechpartstandards.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QPainter>
#include <QTransform>
#include <QPen>
#include <algorithm>

namespace {
const int kIconSize = 130; // 아이콘 버튼의 가로/세로 크기 — 아래 QLabel 폭도 여기 맞춤
}

// ─────────────────────────────────────
// BoltIconButton
// ─────────────────────────────────────
BoltIconButton::BoltIconButton(const QString &label, const QPainterPath &previewPath, BoltKind kind,
                               QWidget *parent)
    : QAbstractButton(parent), m_previewPath(previewPath), m_kind(kind)
{
    setCursor(Qt::PointingHandCursor);
    setToolTip(label); // 명칭은 버튼 아래 별도 QLabel로 표시 — 여기서는 툴팁으로만 보조
}

QSize BoltIconButton::sizeHint() const {
    return QSize(kIconSize, kIconSize);
}

void BoltIconButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    QRect r = rect().adjusted(1, 1, -1, -1);
    QColor bg = isDown() ? QColor(55, 85, 130) : (underMouse() ? QColor(48, 48, 56) : QColor(36, 36, 40));
    p.fillRect(r, bg);
    p.setPen(QPen(underMouse() ? QColor(120, 170, 230) : QColor(85, 85, 90), 1));
    p.drawRect(r);

    // 명칭이 버튼 밖 QLabel로 빠졌으니 아이콘이 버튼 영역 전체(여백만 두고)를 쓴다.
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
            pen.setCosmetic(true); // 확대/축소와 무관하게 화면상 굵기 일정하게
            pen.setWidthF(0.25);
            p.setPen(pen);
            p.drawPath(m_previewPath);
            p.restore();
        }
    }
}

// ─────────────────────────────────────
// BoltPickerDialog
// ─────────────────────────────────────
namespace {

// 규격표에서 라벨이 일치하는 항목을 찾고, 없으면 첫 항목으로 대체한다
// (다른 볼트 SetupDialog들의 selectedSize() 폴백 방식과 동일).
template <typename T>
T pickBoltSize(const QVector<T> &table, const QString &label) {
    for (const T &sz : table) {
        if (sz.label == label) return sz;
    }
    return table.isEmpty() ? T() : table.first();
}

} // namespace

BoltPickerDialog::BoltPickerDialog(QWidget *parent)
    : QDialog(parent), m_selectedKind(BoltKind::Hex)
{
    setWindowTitle(tr("볼트 종류 선택"));
    setupUI();
}

QPainterPath BoltPickerDialog::previewPathFor(BoltKind kind) {
    const double length = 30.0;
    const QPointF origin(0.0, 0.0);
    // angle=90°이면 로컬 (u,v)가 그대로 world(x,y)로 대응해(localToWorld 공식 참고)
    // 머리가 위, 끝단이 아래로 자연스럽게 그려진다.
    const double angle = 90.0;

    switch (kind) {
    case BoltKind::Hex: {
        HexBoltSize size = pickBoltSize(kKsB1002HexBolts(), QStringLiteral("M10"));
        return BoltTool::buildWorldPath(size, length, origin, angle, /*fullThread*/true,
                                        /*includeCenterline*/false, /*includeSide*/false);
    }
    case BoltKind::Wrench: {
        WrenchBoltSize size = pickBoltSize(kKsWrenchBolts(), QStringLiteral("M10"));
        return WrenchBoltTool::buildWorldPath(size, length, origin, angle, true, false, false);
    }
    case BoltKind::Countersunk: {
        HHBoltSize size = pickBoltSize(kKsHHBolts(), QStringLiteral("M10"));
        return HHBoltTool::buildWorldPath(size, length, origin, angle, true, false, false);
    }
    case BoltKind::Button: {
        HBBoltSize size = pickBoltSize(kKsHBBolts(), QStringLiteral("M10"));
        return HBBoltTool::buildWorldPath(size, length, origin, angle, false, false);
    }
    }
    return QPainterPath();
}

void BoltPickerDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    QHBoxLayout *row = new QHBoxLayout;
    row->setSpacing(14);

    struct Entry { BoltKind kind; QString label; };
    static const Entry kEntries[4] = {
        { BoltKind::Hex,         tr("육각볼트") },
        { BoltKind::Wrench,      tr("렌치볼트") },
        { BoltKind::Countersunk, tr("접시머리렌치볼트") },
        { BoltKind::Button,      tr("둥근머리렌치볼트") },
    };

    for (int i = 0; i < 4; ++i) {
        // 아이콘 버튼 + 그 아래 중앙 정렬된 명칭 QLabel을 세로로 묶고, 그 묶음
        // 4개를 가로로 나열한다(요청: "이미지를 가로로 배치, 명칭은 이미지 아래
        // 중앙부에 텍스트라벨로").
        BoltIconButton *btn = new BoltIconButton(kEntries[i].label, previewPathFor(kEntries[i].kind),
                                                 kEntries[i].kind, this);
        connect(btn, &QAbstractButton::clicked, this, &BoltPickerDialog::onIconClicked);

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

    QPushButton *cancelBtn = new QPushButton(tr("취소"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    QHBoxLayout *btnLay = new QHBoxLayout;
    btnLay->addStretch();
    btnLay->addWidget(cancelBtn);
    mainLayout->addLayout(btnLay);
}

void BoltPickerDialog::onIconClicked() {
    BoltIconButton *btn = qobject_cast<BoltIconButton *>(sender());
    if (!btn) return;
    m_selectedKind = btn->kind();
    accept();
}
