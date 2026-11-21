#include "layer3ddialog.h"
#include "appcontext.h"
#include "drawsubwindow.h"
#include "layermanager.h"

#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPixmap>
#include <QTableWidget>
#include <QVBoxLayout>

Layer3DDialog::Layer3DDialog(AppContext *context, QWidget *parent)
    : QDialog(parent), m_appContext(context) {
  setWindowTitle(tr("3D 건축용 - 레이어 높이 설정"));
  resize(420, 460);

  if (m_appContext)
    m_layerManager = m_appContext->layerManager();

  QVBoxLayout *mainLayout = new QVBoxLayout(this);

  // 어느 도면의 레이어인지 표시
  QString docName = tr("(열린 도면 없음)");
  if (m_appContext && m_appContext->activeDrawWindow())
    docName = m_appContext->activeDrawWindow()->windowTitle();
  m_docLabel = new QLabel(tr("도면: %1").arg(docName), this);
  mainLayout->addWidget(m_docLabel);

  QLabel *hint = new QLabel(
      tr("기준높이 = 바닥면 Z, 높이 = 기준높이에서 위로 솟는 높이 (mm).\n"
         "예) 벽 0 / 2400, 창 900 / 1200. 숫자 칸을 더블클릭해 입력합니다.\n"
         "문·창 틀 레이어의 값은 새로 배치하는 문·창의 하단높이 / 개구부 높이가 됩니다 "
         "(높이 0 = 미설정 → 문 0/2100, 창 900/1200). 예외인 문·창은 속성창에서 개별 수정."),
      this);
  hint->setWordWrap(true);
  mainLayout->addWidget(hint);

  m_table = new QTableWidget(this);
  m_table->setColumnCount(ColCount);
  m_table->setHorizontalHeaderLabels(
      QStringList() << tr("레이어") << tr("기준높이 (mm)") << tr("높이 (mm)"));
  m_table->verticalHeader()->setVisible(false);
  m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
  m_table->setEditTriggers(QAbstractItemView::DoubleClicked |
                           QAbstractItemView::EditKeyPressed |
                           QAbstractItemView::AnyKeyPressed);
  m_table->horizontalHeader()->setSectionResizeMode(ColName, QHeaderView::Stretch);
  m_table->horizontalHeader()->setSectionResizeMode(ColElevation, QHeaderView::ResizeToContents);
  m_table->horizontalHeader()->setSectionResizeMode(ColHeight, QHeaderView::ResizeToContents);
  mainLayout->addWidget(m_table);

  QDialogButtonBox *buttons =
      new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(buttons, &QDialogButtonBox::accepted, this, &Layer3DDialog::onAccept);
  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(buttons);

  populate();
}

Layer3DDialog::~Layer3DDialog() {}

void Layer3DDialog::populate() {
  m_table->setRowCount(0);
  if (!m_layerManager)
    return;

  const QList<Layer> layers = m_layerManager->getAllLayers();
  m_table->setRowCount(layers.size());
  for (int i = 0; i < layers.size(); ++i) {
    const Layer &layer = layers.at(i);

    QPixmap pix(14, 14);
    pix.fill(layer.color());
    QTableWidgetItem *nameItem = new QTableWidgetItem(QIcon(pix), layer.name());
    nameItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable); // 이름은 편집 불가
    m_table->setItem(i, ColName, nameItem);

    // EditRole 에 double 을 넣으면 기본 편집기가 숫자 입력(QDoubleSpinBox)이 된다
    QTableWidgetItem *elevItem = new QTableWidgetItem();
    elevItem->setData(Qt::EditRole, layer.elevation());
    elevItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_table->setItem(i, ColElevation, elevItem);

    QTableWidgetItem *heightItem = new QTableWidgetItem();
    heightItem->setData(Qt::EditRole, layer.height());
    heightItem->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_table->setItem(i, ColHeight, heightItem);
  }
}

void Layer3DDialog::onAccept() {
  if (m_layerManager) {
    for (int row = 0; row < m_table->rowCount(); ++row) {
      QTableWidgetItem *nameItem = m_table->item(row, ColName);
      if (!nameItem)
        continue;
      Layer *layer = m_layerManager->getLayer(nameItem->text());
      if (!layer)
        continue;

      bool okE = false;
      bool okH = false;
      const double elev = m_table->item(row, ColElevation)->data(Qt::EditRole).toDouble(&okE);
      const double h = m_table->item(row, ColHeight)->data(Qt::EditRole).toDouble(&okH);
      const bool changed = (okE && elev != layer->elevation()) ||
                           (okH && h != layer->height());
      if (okE)
        layer->setElevation(elev);
      if (okH)
        layer->setHeight(h);
      if (changed)
        emit m_layerManager->layerPropertyChanged(layer->name());
    }
  }
  accept();
}
