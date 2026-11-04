#include "printstyledialog.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "layer.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QColorDialog>
#include <QLabel>

PrintStyleDialog::PrintStyleDialog(IAppContext *ctx, QWidget *parent)
    : QDialog(parent), m_ctx(ctx), m_radioBlackWhite(nullptr),
      m_radioColor(nullptr), m_table(nullptr), m_btnOk(nullptr),
      m_btnCancel(nullptr) {
    setWindowTitle(tr("출력 스타일 편집"));
    setupUI();
    populateTable();
    resize(480, 420);
}

void PrintStyleDialog::setupUI() {
    QVBoxLayout *mainLayout = new QVBoxLayout(this);

    mainLayout->addWidget(new QLabel(
        tr("레이어별로 인쇄에만 쓸 색상을 지정할 수 있습니다.\n"
           "지정하지 않으면(자동) 흰색/노랑처럼 밝은 색은 검정으로 바뀌어 출력됩니다."),
        this));

    // [일괄 적용] 흑백/컬러 라디오버튼 - 아래 테이블 전체 행에 한 번에 적용
    QHBoxLayout *modeRow = new QHBoxLayout();
    modeRow->addWidget(new QLabel(tr("전체 레이어 : "), this));
    m_radioBlackWhite = new QRadioButton(tr("흑백"), this);
    m_radioColor = new QRadioButton(tr("컬러"), this);
    m_radioBlackWhite->setToolTip(tr("모든 레이어의 출력 색상을 검정으로 일괄 지정"));
    m_radioColor->setToolTip(tr("모든 레이어의 출력 색상을 자동으로 일괄 초기화"));
    modeRow->addWidget(m_radioBlackWhite);
    modeRow->addWidget(m_radioColor);
    modeRow->addStretch();
    mainLayout->addLayout(modeRow);

    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    QStringList headers;
    headers << tr("레이어") << tr("화면 색상") << tr("출력 색상") << QString();
    m_table->setHorizontalHeaderLabels(headers);
    m_table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    mainLayout->addWidget(m_table, 1);

    QHBoxLayout *bottomLayout = new QHBoxLayout();
    bottomLayout->addStretch(1);
    m_btnOk = new QPushButton(tr("확인"), this);
    m_btnCancel = new QPushButton(tr("취소"), this);
    m_btnOk->setDefault(true);
    bottomLayout->addWidget(m_btnOk);
    bottomLayout->addWidget(m_btnCancel);
    mainLayout->addLayout(bottomLayout);

    connect(m_radioBlackWhite, &QRadioButton::toggled, this, &PrintStyleDialog::onModeChanged);
    connect(m_radioColor, &QRadioButton::toggled, this, &PrintStyleDialog::onModeChanged);
    connect(m_btnOk, &QPushButton::clicked, this, &PrintStyleDialog::onAccept);
    connect(m_btnCancel, &QPushButton::clicked, this, &PrintStyleDialog::reject);
}

void PrintStyleDialog::populateTable() {
    m_table->setRowCount(0);
    m_editedColors.clear();

    if (!m_ctx || !m_ctx->layerManager())
        return;

    QList<Layer> layers = m_ctx->layerManager()->getAllLayers();
    m_table->setRowCount(layers.size());
    m_editedColors.resize(layers.size());

    for (int row = 0; row < layers.size(); ++row) {
        const Layer &layer = layers.at(row);

        QTableWidgetItem *nameItem = new QTableWidgetItem(layer.name());
        nameItem->setFlags(nameItem->flags() & ~Qt::ItemIsEditable);
        m_table->setItem(row, 0, nameItem);

        // 화면 색상은 읽기 전용 스와치로만 보여준다 (참고용)
        QPushButton *screenSwatch = new QPushButton(this);
        screenSwatch->setEnabled(false);
        screenSwatch->setFixedWidth(50);
        screenSwatch->setStyleSheet(QString("background-color: %1;").arg(layer.color().name()));
        m_table->setCellWidget(row, 1, screenSwatch);

        m_editedColors[row] = layer.hasPrintColorOverride() ? layer.printColor() : QColor();

        QPushButton *printSwatch = new QPushButton(this);
        printSwatch->setFixedWidth(50);
        printSwatch->setProperty("row", row);
        m_table->setCellWidget(row, 2, printSwatch);

        QPushButton *resetBtn = new QPushButton(tr("자동"), this);
        resetBtn->setFixedWidth(40);
        resetBtn->setToolTip(tr("자동 규칙으로 되돌리기"));
        resetBtn->setProperty("row", row);
        m_table->setCellWidget(row, 3, resetBtn);

        connect(printSwatch, &QPushButton::clicked, this, &PrintStyleDialog::onColorButtonClicked);
        connect(resetBtn, &QPushButton::clicked, this, &PrintStyleDialog::onResetButtonClicked);

        updateRowButton(row);
    }

    // [수정] 상단 흑백/컬러 라디오가 현재 레이어 상태를 반영하지 않고 항상 비어있는
    // 상태로 열려서, 실제로는 흑백으로 지정돼 있어도 마치 초기화된(자동) 것처럼
    // 보이는 문제가 있었다. 전체 레이어가 검정으로 지정돼 있으면 "흑백", 전체가
    // 자동(오버라이드 없음)이면 "컬러"를 미리 선택해 준다. 신호는 막아서 onModeChanged가
    // 다시 전체를 덮어쓰지 않도록 한다.
    bool allBlack = !m_editedColors.isEmpty();
    bool allAuto = !m_editedColors.isEmpty();
    for (const QColor &c : m_editedColors) {
        if (c.isValid()) {
            allAuto = false;
            if (c != QColor(Qt::black)) allBlack = false;
        } else {
            allBlack = false;
        }
    }
    m_radioBlackWhite->blockSignals(true);
    m_radioColor->blockSignals(true);
    if (allBlack) {
        m_radioBlackWhite->setChecked(true);
    } else if (allAuto) {
        m_radioColor->setChecked(true);
    } else {
        m_radioBlackWhite->setAutoExclusive(false);
        m_radioColor->setAutoExclusive(false);
        m_radioBlackWhite->setChecked(false);
        m_radioColor->setChecked(false);
        m_radioBlackWhite->setAutoExclusive(true);
        m_radioColor->setAutoExclusive(true);
    }
    m_radioBlackWhite->blockSignals(false);
    m_radioColor->blockSignals(false);
}

void PrintStyleDialog::updateRowButton(int row) {
    if (row < 0 || row >= m_editedColors.size())
        return;

    QPushButton *printSwatch = qobject_cast<QPushButton *>(m_table->cellWidget(row, 2));
    if (!printSwatch)
        return;

    const QColor &c = m_editedColors[row];
    if (c.isValid()) {
        printSwatch->setStyleSheet(QString("background-color: %1;").arg(c.name()));
        printSwatch->setText(QString());
    } else {
        printSwatch->setStyleSheet(QString());
        printSwatch->setText(tr("자동"));
    }
}

void PrintStyleDialog::onColorButtonClicked() {
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;

    int row = btn->property("row").toInt();
    if (row < 0 || row >= m_editedColors.size())
        return;

    QColor initial = m_editedColors[row].isValid() ? m_editedColors[row] : Qt::black;
    QColor picked = QColorDialog::getColor(initial, this, tr("출력 색상 선택"));
    if (picked.isValid()) {
        m_editedColors[row] = picked;
        updateRowButton(row);
    }
}

void PrintStyleDialog::onResetButtonClicked() {
    QPushButton *btn = qobject_cast<QPushButton *>(sender());
    if (!btn)
        return;

    int row = btn->property("row").toInt();
    if (row < 0 || row >= m_editedColors.size())
        return;

    m_editedColors[row] = QColor();
    updateRowButton(row);
}

void PrintStyleDialog::onModeChanged() {
    QRadioButton *btn = qobject_cast<QRadioButton *>(sender());
    // 라디오버튼 특성상 하나가 켜지면 다른 하나는 꺼지며 toggled가 두 번 발생한다.
    // "꺼짐" 쪽 신호는 무시하고, "켜짐" 신호가 온 라디오버튼 기준으로 한 번만 일괄 적용한다.
    if (!btn || !btn->isChecked())
        return;

    // 흑백 = 전체 레이어를 검정으로, 컬러 = 전체 레이어를 자동(오버라이드 해제)으로
    QColor bulkColor = (btn == m_radioBlackWhite) ? QColor(Qt::black) : QColor();
    for (int row = 0; row < m_editedColors.size(); ++row) {
        m_editedColors[row] = bulkColor;
        updateRowButton(row);
    }
}

void PrintStyleDialog::onAccept() {
    if (m_ctx && m_ctx->layerManager()) {
        for (int row = 0; row < m_table->rowCount(); ++row) {
            QTableWidgetItem *nameItem = m_table->item(row, 0);
            if (!nameItem)
                continue;

            Layer *layer = m_ctx->layerManager()->getLayer(nameItem->text());
            if (!layer)
                continue;

            if (m_editedColors[row].isValid()) {
                layer->setPrintColor(m_editedColors[row]);
            } else {
                layer->clearPrintColorOverride();
            }
        }
    }
    accept();
}
