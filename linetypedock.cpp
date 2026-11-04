#include "linetypedock.h"
#include "AutoHideMessage.h"
#include "appcontext.h"
#include "linetypeeditdialog.h"
#include "linetypemanager.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QStandardItem>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>
#include <QVBoxLayout>

LineTypeDock::LineTypeDock(AppContext *context, QWidget *parent)
    : QDockWidget(tr("라인타입 목록"), parent), m_appContext(context) {
  this->setObjectName("LineTypeDock");
  if (m_appContext && m_appContext->lineTypeManager()) {
    LineTypeManager *manager = m_appContext->lineTypeManager();
    linetypeFile = manager->getDefaultFilePath();

    if (!manager->loadFromFile(linetypeFile)) {
      manager->addLinetype(LineType("Continuous", "_______"));
      manager->saveToFile(linetypeFile);
    } else {
      AutoHideMessage *popup =
          new AutoHideMessage("linetype.lin loading..", 1000, this);
      popup->show();
    }
  }

  QWidget *mainWidget = new QWidget(this);
  mainWidget->setObjectName("LineTypeDockContent");

  QVBoxLayout *mainLayout = new QVBoxLayout(mainWidget);

  tableView = new QTableView(this);
  // [신규] ThemeManager에서 타겟팅할 수 있도록 ObjectName 설정
  tableView->setObjectName("LineTypeTableView");

  model = new QStandardItemModel(this);
  model->setColumnCount(2);
  model->setHeaderData(0, Qt::Horizontal, tr("이름"));
  model->setHeaderData(1, Qt::Horizontal, tr("패턴"));
  tableView->setModel(model);
  tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableView->setSelectionMode(QAbstractItemView::SingleSelection);
  tableView->verticalHeader()->setVisible(false);
  tableView->setIconSize(QSize(48, 16)); // 선종류 모양 미리보기
  tableView->setColumnWidth(0, 120);
  tableView->setColumnWidth(1, 200);

  // 하드코딩된 QTableView 스타일시트 제거됨 (ThemeManager로 이동)

  mainLayout->addWidget(tableView);

  QToolBar *pentoolBar = new QToolBar(tr("LineTypeToolbar"), this);
  // [신규] ThemeManager에서 타겟팅할 수 있도록 ObjectName 설정
  pentoolBar->setObjectName("LineTypeToolBar");
  pentoolBar->setIconSize(QSize(16, 16));

  QToolButton *makeButton = new QToolButton(this);
  makeButton->setIcon(QIcon(":/icons/makeItem.png"));
  makeButton->setToolTip(tr("Add new LineType"));
  pentoolBar->addWidget(makeButton);

  QToolButton *editButton = new QToolButton(this);
  editButton->setIcon(QIcon(":/icons/editItem.png"));
  editButton->setToolTip(tr("Edit selected LineType"));
  pentoolBar->addWidget(editButton);

  QToolButton *eraseButton = new QToolButton(this);
  eraseButton->setIcon(QIcon(":/icons/eraseItem.png"));
  eraseButton->setToolTip(tr("Delete selected LineType"));
  pentoolBar->addWidget(eraseButton);

  QToolButton *saveButton = new QToolButton(this);
  saveButton->setIcon(QIcon(":/icons/saveItem.png"));
  saveButton->setToolTip(tr("Save LineTypes to file"));
  pentoolBar->addWidget(saveButton);

  // 하드코딩된 QToolBar 스타일시트 제거됨 (ThemeManager로 이동)

  mainLayout->addWidget(pentoolBar);
  setWidget(mainWidget);

  populateTable();

  // 포인터 함수 기반 시그널-슬롯 연결
  connect(makeButton, &QToolButton::clicked, this, &LineTypeDock::onMakeItem);
  connect(editButton, &QToolButton::clicked, this, &LineTypeDock::onEditItem);
  connect(eraseButton, &QToolButton::clicked, this, &LineTypeDock::onEraseItem);
  connect(saveButton, &QToolButton::clicked, this, &LineTypeDock::onSaveItem);
}

LineTypeDock::~LineTypeDock() {}

void LineTypeDock::populateTable() {
  if (!m_appContext || !m_appContext->lineTypeManager())
    return;

  model->removeRows(0, model->rowCount());
  LineTypeManager *ltm = m_appContext->lineTypeManager();
  QList<LineType> types = ltm->getAllLinetypes();

  // 인덱스 기반 루프 사용
  for (int i = 0; i < types.size(); ++i) {
    const LineType &lt = types.at(i);
    QStandardItem *rowItemName = new QStandardItem(lt.name);
    QStandardItem *rowItemPattern = new QStandardItem(lt.pattern);
    // 준비된 PNG 대신 대시 패턴에서 모양을 직접 그린다.
    rowItemName->setIcon(ltm->previewIconFor(lt.name, QSize(48, 14)));
    rowItemName->setEditable(false);
    rowItemPattern->setEditable(false);

    QList<QStandardItem *> rowItems;
    rowItems.append(rowItemName);
    rowItems.append(rowItemPattern);
    model->appendRow(rowItems);
  }
}

// ... (onMakeItem, onEditItem, onEraseItem, onSaveItem 구현부는 기존과 동일)
// ...

void LineTypeDock::onMakeItem() {
  if (!m_appContext || !m_appContext->lineTypeManager())
    return;

  LineTypeEditDialog dialog(this);
  dialog.setWindowTitle(tr("Add New LineType"));
  if (dialog.exec() == QDialog::Accepted) {
    QString name = dialog.getName();
    QString pattern = dialog.getPattern();

    if (name.isEmpty() || pattern.isEmpty()) {
      QMessageBox::warning(this, tr("Input Error"),
                           tr("Name and Pattern cannot be empty."));
      return;
    }

    LineType lt(name, pattern);
    m_appContext->lineTypeManager()->addLinetype(lt);

    QList<QStandardItem *> rowItems;
    rowItems.append(new QStandardItem(name));
    rowItems.append(new QStandardItem(pattern));
    model->appendRow(rowItems);

    AutoHideMessage *popup =
        new AutoHideMessage(tr("linetype add .. ok.."), 500, this);
    popup->show();
  }
}

void LineTypeDock::onEditItem() {
  if (!m_appContext || !m_appContext->lineTypeManager())
    return;

  QModelIndexList selected = tableView->selectionModel()->selectedRows();
  if (selected.isEmpty()) {
    QMessageBox::warning(this, tr("Selection Error"),
                         tr("Please select an item to edit."));
    return;
  }

  int row = selected.first().row();
  QString currentName = model->item(row, 0)->text();
  QString currentPattern = model->item(row, 1)->text();

  LineTypeEditDialog dialog(this);
  dialog.setWindowTitle(tr("Edit LineType"));
  dialog.setName(currentName);
  dialog.setPattern(currentPattern);

  if (dialog.exec() == QDialog::Accepted) {
    QString newName = dialog.getName();
    QString newPattern = dialog.getPattern();

    if (newName.isEmpty() || newPattern.isEmpty()) {
      QMessageBox::warning(this, tr("Input Error"),
                           tr("Name and Pattern cannot be empty."));
      return;
    }

    model->item(row, 0)->setText(newName);
    model->item(row, 1)->setText(newPattern);

    m_appContext->lineTypeManager()->editLinetype(
        currentName, LineType(newName, newPattern));

    AutoHideMessage *popup =
        new AutoHideMessage(tr("Select Item update.. ok.."), 500, this);
    popup->show();
  }
}

void LineTypeDock::onEraseItem() {
  if (!m_appContext || !m_appContext->lineTypeManager())
    return;

  QModelIndexList selected = tableView->selectionModel()->selectedRows();
  if (selected.isEmpty()) {
    QMessageBox::warning(this, tr("Selection Error"),
                         tr("Please select an item to delete."));
    return;
  }

  int row = selected.first().row();
  QString removeName = model->item(row, 0)->text();

  m_appContext->lineTypeManager()->removeLinetype(removeName);
  model->removeRow(row);

  AutoHideMessage *popup =
      new AutoHideMessage(tr("Select item remove..ok.."), 500, this);
  popup->show();
}

void LineTypeDock::onSaveItem() {
  if (!m_appContext || !m_appContext->lineTypeManager())
    return;

  if (m_appContext->lineTypeManager()->saveToFile(linetypeFile)) {
    AutoHideMessage *popup =
        new AutoHideMessage(tr("linetype.lin Saving.. ok.."), 500, this);
    popup->show();
  }
}