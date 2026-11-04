#include "drawingsettingsdialog.h"
#include "layer.h"
#include "layermanager.h"
#include <QPushButton>

DrawingSettingsDialog::DrawingSettingsDialog(LayerManager *layerMgr,
                                             QWidget *parent)
    : QDialog(parent) {
  setWindowTitle(tr("도면틀 설정 하기"));
  setMinimumWidth(300);

  QVBoxLayout *mainLayout = new QVBoxLayout(this);

  // 1. Drawing Size Group
  QGroupBox *sizeGroup = new QGroupBox(tr("도면틀 크기"), this);
  QVBoxLayout *sizeLayout = new QVBoxLayout(sizeGroup);

  QHBoxLayout *basicRow = new QHBoxLayout();
  m_radioBasic = new QRadioButton(tr("일반 틀 (Overlay Guide)"), sizeGroup);
  m_comboBasic = new QComboBox(sizeGroup);
  m_comboBasic->addItems({"None", "A4 (297x210)", "A3 (420x297)",
                          "A2 (594x420)", "A1 (841x594)", "A0 (1189x841)"});
  basicRow->addWidget(m_radioBasic);
  basicRow->addWidget(m_comboBasic);
  sizeLayout->addLayout(basicRow);

  QHBoxLayout *userRow = new QHBoxLayout();
  m_radioUser = new QRadioButton(tr("사용자 틀 (Insert CDB Frame)"), sizeGroup);
  m_comboUser = new QComboBox(sizeGroup);
  m_comboUser->addItems({"A4", "A3", "A2", "A1", "A0"});
  userRow->addWidget(m_radioUser);
  userRow->addWidget(m_comboUser);
  sizeLayout->addLayout(userRow);

  mainLayout->addWidget(sizeGroup);

  // 2. Drawing Scale Group
  QGroupBox *scaleGroup = new QGroupBox(tr("도면 스케일"), this);
  QHBoxLayout *scaleLayout = new QHBoxLayout(scaleGroup);
  scaleLayout->addWidget(new QLabel("1 / ", scaleGroup));
  m_scaleEdit = new QLineEdit("1", scaleGroup);
  scaleLayout->addWidget(m_scaleEdit);

  m_comboScale = new QComboBox(scaleGroup);
  m_comboScale->addItems(
      {"Custom", "10", "20", "25", "50", "75", "100", "150", "200"});
  scaleLayout->addWidget(m_comboScale);

  m_btnPickScale = new QPushButton(tr("Pick"), scaleGroup);
  m_btnPickScale->setFixedWidth(60);
  scaleLayout->addWidget(m_btnPickScale);

  mainLayout->addWidget(scaleGroup);

  // 2-b. 단위 그룹 (도면 스코프: DocumentSettings "units/linear")
  QGroupBox *unitGroup = new QGroupBox(tr("단위"), this);
  QHBoxLayout *unitLayout = new QHBoxLayout(unitGroup);
  m_comboUnit = new QComboBox(unitGroup);
  m_comboUnit->addItem(tr("밀리미터 (mm)"), static_cast<int>(LinearUnit::Mm));
  m_comboUnit->addItem(tr("인치 (inch)"), static_cast<int>(LinearUnit::Inch));
  unitLayout->addWidget(m_comboUnit);
  mainLayout->addWidget(unitGroup);

  // 3. Center Line Layer Group
  QGroupBox *clGroup = new QGroupBox(tr("중심선 레이어 설정"), this);
  QFormLayout *clForm = new QFormLayout(clGroup);
  clForm->setLabelAlignment(Qt::AlignRight);

  m_comboCenterLayer = new QComboBox(clGroup);
  m_comboCenterLayer->setObjectName("CenterLineLayerCombo");
  m_comboCenterLayer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  m_comboCenterLayer->setMaximumHeight(33);
  m_comboCenterLayer->setEditable(true);
  if (m_comboCenterLayer->lineEdit()) {
    m_comboCenterLayer->lineEdit()->setReadOnly(true);
    m_comboCenterLayer->lineEdit()->setAlignment(Qt::AlignCenter);
  }
  populateLayerCombo(layerMgr, QString());

  clForm->addRow(tr("중심선 레이어 :"), m_comboCenterLayer);
  mainLayout->addWidget(clGroup);

  // Buttons
  m_buttonBox = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
  connect(m_buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
  connect(m_buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
  mainLayout->addWidget(m_buttonBox);

  // Connections
  connect(m_radioBasic, &QRadioButton::toggled, this,
          &DrawingSettingsDialog::onModeToggled);
  connect(m_radioUser, &QRadioButton::toggled, this,
          &DrawingSettingsDialog::onModeToggled);
  connect(m_btnPickScale, &QPushButton::clicked, this,
          &DrawingSettingsDialog::pickScaleRequested);
  connect(m_comboScale, QOverload<int>::of(&QComboBox::activated), this,
          &DrawingSettingsDialog::onScalePresetChanged);

  m_radioBasic->setChecked(true);
  onModeToggled(true);
}

void DrawingSettingsDialog::populateLayerCombo(LayerManager *mgr,
                                               const QString &selectedName) {
  m_comboCenterLayer->blockSignals(true);
  m_comboCenterLayer->clear();
  m_comboCenterLayer->addItem(tr("기본값 (현재 레이어)"), QString());

  if (mgr) {
    for (const Layer &layer : mgr->getAllLayers()) {
      QPixmap px(16, 16);
      px.fill(layer.color());
      m_comboCenterLayer->addItem(QIcon(px), layer.name(), layer.name());
    }
  }

  int idx = selectedName.isEmpty() ? 0
                                   : m_comboCenterLayer->findData(selectedName);
  m_comboCenterLayer->setCurrentIndex(idx >= 0 ? idx : 0);
  m_comboCenterLayer->blockSignals(false);
}

// ── 접근자 ────────────────────────────────────────────────────────────────────

DrawingSettingsDialog::SizeMode DrawingSettingsDialog::sizeMode() const {
  return m_radioBasic->isChecked() ? Mode_Basic : Mode_User;
}

QString DrawingSettingsDialog::selectedSize() const {
  if (sizeMode() == Mode_Basic) {
    QString txt = m_comboBasic->currentText();
    return txt.split(" ").first();
  } else {
    return m_comboUser->currentText();
  }
}

double DrawingSettingsDialog::drawingScale() const {
  bool ok;
  double val = m_scaleEdit->text().toDouble(&ok);
  return (ok && val > 0) ? val : 1.0;
}

LinearUnit DrawingSettingsDialog::linearUnit() const {
  return static_cast<LinearUnit>(m_comboUnit->currentData().toInt());
}

void DrawingSettingsDialog::setLinearUnit(LinearUnit u) {
  int idx = m_comboUnit->findData(static_cast<int>(u));
  m_comboUnit->setCurrentIndex(idx >= 0 ? idx : 0);
}

void DrawingSettingsDialog::setDrawingScale(double scale) {
  QString scaleStr = QString::number(scale, 'f', 2);
  m_scaleEdit->setText(scaleStr);

  int val = qRound(scale);
  int idx = m_comboScale->findText(QString::number(val));
  if (idx != -1 && std::abs(scale - val) < 0.01)
    m_comboScale->setCurrentIndex(idx);
  else
    m_comboScale->setCurrentIndex(0);
}

QString DrawingSettingsDialog::centerLineLayer() const {
  return m_comboCenterLayer->currentData().toString();
}

void DrawingSettingsDialog::setCenterLineLayer(const QString &layerName) {
  int idx = layerName.isEmpty() ? 0 : m_comboCenterLayer->findData(layerName);
  m_comboCenterLayer->setCurrentIndex(idx >= 0 ? idx : 0);
}

void DrawingSettingsDialog::setInitialValues(SizeMode mode, const QString &size,
                                             double scale) {
  if (mode == Mode_Basic) {
    m_radioBasic->setChecked(true);
    int idx = m_comboBasic->findText(size, Qt::MatchStartsWith);
    if (idx != -1)
      m_comboBasic->setCurrentIndex(idx);
  } else {
    m_radioUser->setChecked(true);
    m_comboUser->setCurrentText(size);
  }
  m_scaleEdit->setText(QString::number(scale));
}

void DrawingSettingsDialog::onScalePresetChanged(int index) {
  if (index > 0)
    m_scaleEdit->setText(m_comboScale->currentText());
}

void DrawingSettingsDialog::onModeToggled(bool) {
  m_comboBasic->setEnabled(m_radioBasic->isChecked());
  m_comboUser->setEnabled(m_radioUser->isChecked());
}
