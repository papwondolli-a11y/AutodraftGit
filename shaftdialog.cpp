#include "shaftdialog.h"
#include "shaftbuilder.h"
#include "shaftenddialog.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QPixmap>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
enum Column {
    ColDiameter = 0,
    ColLength,
    ColFit,
    ColChamferL,
    ColChamferR,
    ColDimDia,
    ColShowFit,
    ColDimLen,
    ColCount
};

QString numText(double v) {
    return QString::number(v, 'f', 3).remove(QRegularExpression(QStringLiteral("\\.?0+$")));
}

QTableWidgetItem *checkItem(bool on) {
    QTableWidgetItem *it = new QTableWidgetItem;
    it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    it->setCheckState(on ? Qt::Checked : Qt::Unchecked);
    return it;
}
} // namespace

ShaftDialog::ShaftDialog(IAppContext *ctx, QWidget *parent)
    : QDialog(parent), m_ctx(ctx)
{
    setWindowTitle(tr("축 (Shaft)"));
    resize(640, 460);
    setupUI();
}

void ShaftDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    m_table = new QTableWidget(0, ColCount, this);
    QStringList headers;
    headers << tr("지름") << tr("길이") << tr("끼워맞춤") << tr("좌 모따기") << tr("우 모따기")
            << tr("지름 치수") << tr("끼워맞춤\n공차 표시") << tr("길이 치수");
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    mainLayout->addWidget(m_table);
    connect(m_table, &QTableWidget::itemChanged, this, &ShaftDialog::updateTotal);
    // 행 번호(세로 헤더) 또는 "길이 치수" 칸을 더블클릭하면 길이 공차 입력창이 뜬다.
    connect(m_table, &QTableWidget::cellDoubleClicked, this, &ShaftDialog::onCellDoubleClicked);
    connect(m_table->verticalHeader(), &QHeaderView::sectionDoubleClicked, this, &ShaftDialog::editLengthTolerance);

    QHBoxLayout *btnRow = new QHBoxLayout;
    QPushButton *addBtn    = new QPushButton(tr("구간 추가"), this);
    QPushButton *delBtn    = new QPushButton(tr("구간 삭제"), this);
    QPushButton *upBtn     = new QPushButton(tr("▲ 위로(왼쪽)"), this);
    QPushButton *downBtn   = new QPushButton(tr("▼ 아래로(오른쪽)"), this);
    QPushButton *sampleBtn = new QPushButton(tr("예시 불러오기"), this);
    connect(addBtn,    &QPushButton::clicked, this, &ShaftDialog::addSegment);
    connect(delBtn,    &QPushButton::clicked, this, &ShaftDialog::removeSegment);
    connect(upBtn,     &QPushButton::clicked, this, &ShaftDialog::moveUp);
    connect(downBtn,   &QPushButton::clicked, this, &ShaftDialog::moveDown);
    connect(sampleBtn, &QPushButton::clicked, this, &ShaftDialog::loadSample);
    btnRow->addWidget(addBtn);
    btnRow->addWidget(delBtn);
    btnRow->addWidget(upBtn);
    btnRow->addWidget(downBtn);
    btnRow->addStretch(1);
    btnRow->addWidget(sampleBtn);
    mainLayout->addLayout(btnRow);

    m_totalLabel = new QLabel(this);
    mainLayout->addWidget(m_totalLabel);

    // 양쪽 끝단 형상: 없음 / 키홈 / 나사부 (선택하면 크기 입력창이 뜬다)
    QFormLayout *endForm = new QFormLayout;
    m_leftCombo  = new QComboBox(this);
    m_rightCombo = new QComboBox(this);
    m_leftEdit   = new QPushButton(tr("크기 설정..."), this);
    m_rightEdit  = new QPushButton(tr("크기 설정..."), this);
    const QStringList endNames = QStringList() << tr("없음") << tr("키홈") << tr("나사부");
    m_leftCombo->addItems(endNames);
    m_rightCombo->addItems(endNames);
    m_leftEdit->setEnabled(false);
    m_rightEdit->setEnabled(false);
    connect(m_leftCombo,  QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ShaftDialog::onLeftEndChanged);
    connect(m_rightCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ShaftDialog::onRightEndChanged);
    connect(m_leftEdit,   &QPushButton::clicked, this, &ShaftDialog::editLeftEnd);
    connect(m_rightEdit,  &QPushButton::clicked, this, &ShaftDialog::editRightEnd);
    QHBoxLayout *leftRow = new QHBoxLayout;
    leftRow->addWidget(m_leftCombo, 1);
    leftRow->addWidget(m_leftEdit);
    QHBoxLayout *rightRow = new QHBoxLayout;
    rightRow->addWidget(m_rightCombo, 1);
    rightRow->addWidget(m_rightEdit);
    endForm->addRow(tr("왼쪽 끝단 :"), leftRow);
    endForm->addRow(tr("오른쪽 끝단 :"), rightRow);
    mainLayout->addLayout(endForm);

    m_centerLine   = new QCheckBox(tr("중심선 그리기"), this);
    m_diameterDims = new QCheckBox(tr("지름 치수 생성"), this);
    m_lengthDims   = new QCheckBox(tr("길이 치수 생성 (구간별 + 전체)"), this);
    mainLayout->addWidget(m_centerLine);
    mainLayout->addWidget(m_diameterDims);
    mainLayout->addWidget(m_lengthDims);

    QFormLayout *form = new QFormLayout;
    m_dimGap = new QDoubleSpinBox(this);
    m_dimGap->setRange(0.0, 100000.0);
    m_dimGap->setSuffix(QStringLiteral(" mm"));
    m_dimStep = new QDoubleSpinBox(this);
    m_dimStep->setRange(0.1, 100000.0);
    m_dimStep->setSuffix(QStringLiteral(" mm"));
    form->addRow(tr("첫 치수선까지 거리 :"), m_dimGap);
    form->addRow(tr("치수선 단 간격 :"), m_dimStep);

    // 축 레이어: 색상 아이콘이 붙은 레이어 콤보. 첫 항목(데이터 "")은 기본 SHAFT 레이어.
    m_layerCombo = new QComboBox(this);
    {
        QPixmap defPix(14, 14);
        defPix.fill(Qt::white);
        m_layerCombo->addItem(QIcon(defPix), tr("SHAFT (기본)"), QString());
        if (m_ctx && m_ctx->layerManager()) {
            const QList<Layer> layers = m_ctx->layerManager()->getAllLayers();
            for (int i = 0; i < layers.size(); ++i) {
                QPixmap pix(14, 14);
                pix.fill(layers[i].color());
                m_layerCombo->addItem(QIcon(pix), layers[i].name(), layers[i].name());
            }
        }
    }
    form->addRow(tr("축 레이어 :"), m_layerCombo);
    mainLayout->addLayout(form);

    QLabel *hint = new QLabel(
        tr("행 하나가 축의 한 구간입니다(왼쪽 끝 → 오른쪽 끝). 칼라·홈은 지름이 다른 짧은 구간으로 입력하세요.\n"
           "기준점은 축 왼쪽 끝면의 중심이며, 끼워맞춤은 g6, k5 처럼 입력합니다(비우면 공차 없음)."),
        this);
    hint->setWordWrap(true);
    mainLayout->addWidget(hint);

    QDialogButtonBox *buttons =
        new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &ShaftDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &ShaftDialog::reject);
    mainLayout->addWidget(buttons);
}

QString ShaftDialog::cellText(int row, int col) const {
    const QTableWidgetItem *it = m_table->item(row, col);
    return it ? it->text() : QString();
}

namespace {
const int kTolRole = Qt::UserRole + 1; // 길이 치수 칸에 저장하는 공차 (QVariantList: 사용, 상한, 하한)

void storeLenTol(QTableWidgetItem *it, bool use, double upper, double lower) {
    if (!it) return;
    it->setData(kTolRole, QVariantList() << use << upper << lower);
    it->setText(use ? QStringLiteral("+%1/%2").arg(upper).arg(lower) : QString());
    it->setToolTip(use ? QObject::tr("길이 공차: 상한 %1 / 하한 %2").arg(upper).arg(lower)
                       : QObject::tr("더블클릭: 길이 공차 입력"));
}
}

void ShaftDialog::onCellDoubleClicked(int row, int col) {
    if (col == ColDimLen) editLengthTolerance(row);
}

void ShaftDialog::editLengthTolerance(int row) {
    QTableWidgetItem *it = m_table->item(row, ColDimLen);
    if (!it) return;
    const QVariantList cur = it->data(kTolRole).toList();
    const bool use = cur.size() == 3 && cur.at(0).toBool();

    QDialog dlg(this);
    dlg.setWindowTitle(tr("구간 %1 길이 공차").arg(row + 1));
    QVBoxLayout *lay = new QVBoxLayout(&dlg);
    QCheckBox *useChk = new QCheckBox(tr("길이 치수에 공차 표시"), &dlg);
    useChk->setChecked(use);
    lay->addWidget(useChk);
    QFormLayout *form = new QFormLayout;
    QDoubleSpinBox *upper = new QDoubleSpinBox(&dlg);
    QDoubleSpinBox *lower = new QDoubleSpinBox(&dlg);
    for (QDoubleSpinBox *sp : {upper, lower}) {
        sp->setDecimals(3);
        sp->setRange(-1000.0, 1000.0);
        sp->setSuffix(QStringLiteral(" mm"));
        sp->setEnabled(use);
    }
    if (cur.size() == 3) { upper->setValue(cur.at(1).toDouble()); lower->setValue(cur.at(2).toDouble()); }
    form->addRow(tr("상한 편차 (예: +0.01) :"), upper);
    form->addRow(tr("하한 편차 (예: 0, -0.02) :"), lower);
    lay->addLayout(form);
    connect(useChk, &QCheckBox::toggled, upper, &QWidget::setEnabled);
    connect(useChk, &QCheckBox::toggled, lower, &QWidget::setEnabled);
    QDialogButtonBox *bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(bb);
    if (dlg.exec() != QDialog::Accepted) return;

    if (useChk->isChecked() && upper->value() < lower->value()) {
        QMessageBox::warning(this, tr("길이 공차"), tr("상한 편차는 하한 편차보다 작을 수 없습니다."));
        return;
    }
    const bool wasBlocked = m_table->blockSignals(true);
    storeLenTol(it, useChk->isChecked(), upper->value(), lower->value());
    m_table->blockSignals(wasBlocked);
}

void ShaftDialog::appendRow(const ShaftSegment &seg) {
    // 셀을 하나씩 채우는 동안 itemChanged → updateTotal 이 빈 셀(null)을 읽지 않도록 막는다.
    const bool wasBlocked = m_table->blockSignals(true);
    const int row = m_table->rowCount();
    m_table->insertRow(row);
    m_table->setItem(row, ColDiameter, new QTableWidgetItem(numText(seg.diameter)));
    m_table->setItem(row, ColLength,   new QTableWidgetItem(numText(seg.length)));
    m_table->setItem(row, ColFit,      new QTableWidgetItem(seg.fit));
    m_table->setItem(row, ColChamferL, new QTableWidgetItem(numText(seg.chamferLeft)));
    m_table->setItem(row, ColChamferR, new QTableWidgetItem(numText(seg.chamferRight)));
    m_table->setItem(row, ColDimDia,   checkItem(seg.dimDiameter));
    m_table->setItem(row, ColShowFit,  checkItem(seg.showFit));
    m_table->setItem(row, ColDimLen,   checkItem(seg.dimLength));
    storeLenTol(m_table->item(row, ColDimLen), seg.lenTolUse, seg.lenTolUpper, seg.lenTolLower);
    m_table->blockSignals(wasBlocked);
}

void ShaftDialog::setParams(const ShaftParams &params) {
    m_table->blockSignals(true);
    m_table->setRowCount(0);
    for (int i = 0; i < params.segments.size(); ++i) appendRow(params.segments.at(i));
    m_table->blockSignals(false);

    m_leftEnd  = params.leftEnd;
    m_rightEnd = params.rightEnd;
    syncEndWidgets(0);
    syncEndWidgets(1);

    m_centerLine->setChecked(params.drawCenterLine);
    m_diameterDims->setChecked(params.diameterDims);
    m_lengthDims->setChecked(params.lengthDims);
    m_dimGap->setValue(params.dimGap);
    m_dimStep->setValue(params.dimStep);
    {
        const int idx = m_layerCombo->findData(params.bodyLayer);
        m_layerCombo->setCurrentIndex(idx >= 0 ? idx : 0); // 없는 레이어면 기본으로
    }
    updateTotal();
}

ShaftParams ShaftDialog::params() const {
    ShaftParams p;
    for (int row = 0; row < m_table->rowCount(); ++row) {
        ShaftSegment s;
        s.diameter     = cellText(row, ColDiameter).toDouble();
        s.length       = cellText(row, ColLength).toDouble();
        s.fit          = cellText(row, ColFit).trimmed();
        s.chamferLeft  = cellText(row, ColChamferL).toDouble();
        s.chamferRight = cellText(row, ColChamferR).toDouble();
        const QTableWidgetItem *dd = m_table->item(row, ColDimDia);
        const QTableWidgetItem *dl = m_table->item(row, ColDimLen);
        s.dimDiameter  = !dd || dd->checkState() == Qt::Checked;
        const QTableWidgetItem *sf = m_table->item(row, ColShowFit);
        s.showFit      = !sf || sf->checkState() == Qt::Checked;
        const QVariantList tol = dl ? dl->data(kTolRole).toList() : QVariantList();
        if (tol.size() == 3) {
            s.lenTolUse   = tol.at(0).toBool();
            s.lenTolUpper = tol.at(1).toDouble();
            s.lenTolLower = tol.at(2).toDouble();
        }
        s.dimLength    = !dl || dl->checkState() == Qt::Checked;
        p.segments.append(s);
    }
    p.leftEnd  = m_leftEnd;
    p.rightEnd = m_rightEnd;
    p.drawCenterLine = m_centerLine->isChecked();
    p.diameterDims   = m_diameterDims->isChecked();
    p.lengthDims     = m_lengthDims->isChecked();
    p.dimGap         = m_dimGap->value();
    p.dimStep        = m_dimStep->value();
    p.bodyLayer      = m_layerCombo->currentData().toString();
    return p;
}

void ShaftDialog::updateTotal() {
    const ShaftParams p = params();
    m_totalLabel->setText(tr("구간 %1개 · 전체 길이 %2 mm · 최대 지름 %3 mm")
                              .arg(p.segments.size())
                              .arg(numText(p.totalLength()))
                              .arg(numText(p.maxDiameter())));
}

void ShaftDialog::addSegment() {
    ShaftSegment seg;
    const int cur = m_table->rowCount();
    if (cur > 0) {
        seg.diameter = cellText(cur - 1, ColDiameter).toDouble();
        seg.length   = cellText(cur - 1, ColLength).toDouble();
    }
    appendRow(seg);
    m_table->selectRow(m_table->rowCount() - 1);
    updateTotal();
}

void ShaftDialog::removeSegment() {
    const int row = m_table->currentRow();
    if (row < 0) return;
    m_table->removeRow(row);
    updateTotal();
}

void ShaftDialog::swapRows(int a, int b) {
    if (a < 0 || b < 0 || a >= m_table->rowCount() || b >= m_table->rowCount()) return;
    m_table->blockSignals(true);
    for (int col = 0; col < ColCount; ++col) {
        QTableWidgetItem *ia = m_table->takeItem(a, col);
        QTableWidgetItem *ib = m_table->takeItem(b, col);
        m_table->setItem(a, col, ib);
        m_table->setItem(b, col, ia);
    }
    m_table->blockSignals(false);
    m_table->selectRow(b);
    updateTotal();
}

void ShaftDialog::moveUp() {
    const int row = m_table->currentRow();
    if (row > 0) swapRows(row, row - 1);
}

void ShaftDialog::moveDown() {
    const int row = m_table->currentRow();
    if (row >= 0 && row + 1 < m_table->rowCount()) swapRows(row, row + 1);
}

void ShaftDialog::syncEndWidgets(int side) {
    QComboBox *combo = comboRef(side);
    combo->blockSignals(true);
    combo->setCurrentIndex(int(endRef(side).type));
    combo->blockSignals(false);
    editBtnRef(side)->setEnabled(endRef(side).type != ShaftEndType::None);
}

bool ShaftDialog::editEnd(int side) {
    ShaftEnd &end = endRef(side);
    if (end.type == ShaftEndType::None) return true;

    // 끝 구간(첫/마지막 행)의 지름·길이를 안내용으로 넘긴다.
    const int rows = m_table->rowCount();
    double segD = 0.0;
    double segL = 0.0;
    if (rows > 0) {
        const int row = (side == 0) ? 0 : rows - 1;
        segD = cellText(row, ColDiameter).toDouble();
        segL = cellText(row, ColLength).toDouble();
    }

    ShaftEndDialog dlg(end.type, segD, segL, this);
    if (end.type == ShaftEndType::Keyway) {
        ShaftKeyway kw = end.keyway;
        if (kw.sectionPos < 0) kw.sectionPos = (side == 0) ? 0 : 2; // 기본: 끝단 쪽
        dlg.setKeyway(kw);
    }
    else                                  dlg.setThread(end.thread);
    if (dlg.exec() != QDialog::Accepted) return false;

    if (end.type == ShaftEndType::Keyway) end.keyway = dlg.keyway();
    else                                  end.thread = dlg.thread();
    return true;
}

void ShaftDialog::changeEnd(int side, int index) {
    ShaftEnd &end = endRef(side);
    const ShaftEndType prev = end.type;
    ShaftEndType next = ShaftEndType::None;
    if (index == 1) next = ShaftEndType::Keyway;
    else if (index == 2) next = ShaftEndType::Thread;

    end.type = next;
    // 형상을 고르면 바로 크기 입력창을 띄운다. 취소하면 이전 선택으로 되돌린다.
    if (next != ShaftEndType::None && !editEnd(side)) end.type = prev;
    syncEndWidgets(side);
}

void ShaftDialog::onLeftEndChanged(int index)  { changeEnd(0, index); }
void ShaftDialog::onRightEndChanged(int index) { changeEnd(1, index); }
void ShaftDialog::editLeftEnd()  { editEnd(0); }
void ShaftDialog::editRightEnd() { editEnd(1); }

void ShaftDialog::loadSample() {
    setParams(ShaftParams::sample());
}

void ShaftDialog::accept() {
    const QStringList problems = ShaftBuilder::validate(params());
    if (!problems.isEmpty()) {
        QMessageBox::warning(this, tr("확인"), problems.join(QLatin1Char('\n')));
        return;
    }
    QDialog::accept();
}
