#ifndef DRAWINGSETTINGSDIALOG_H
#define DRAWINGSETTINGSDIALOG_H

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QFormLayout>
#include <QRadioButton>
#include <QVBoxLayout>

#include "unitconvert.h"

class LayerManager;

class DrawingSettingsDialog : public QDialog {
  Q_OBJECT
public:
  explicit DrawingSettingsDialog(LayerManager *layerMgr,
                                 QWidget *parent = nullptr);

  enum SizeMode { Mode_Basic, Mode_User };

  SizeMode sizeMode() const;
  QString selectedSize() const;
  double drawingScale() const;
  void setDrawingScale(double scale);

  // [Phase 7] 도면 길이 단위 (mm / inch)
  LinearUnit linearUnit() const;
  void setLinearUnit(LinearUnit u);

  // 중심선 레이어 (빈 문자열 = 현재 레이어 사용)
  QString centerLineLayer() const;
  void setCenterLineLayer(const QString &layerName);

  void setInitialValues(SizeMode mode, const QString &size, double scale);

signals:
  void pickScaleRequested();

private slots:
  void onModeToggled(bool checked);
  void onScalePresetChanged(int index);

private:
  void populateLayerCombo(LayerManager *mgr, const QString &selectedName);

  QRadioButton *m_radioBasic;
  QRadioButton *m_radioUser;
  QComboBox    *m_comboBasic;
  QComboBox    *m_comboUser;
  QLineEdit    *m_scaleEdit;
  QComboBox    *m_comboScale;
  QPushButton  *m_btnPickScale;
  QComboBox    *m_comboUnit;
  QComboBox    *m_comboCenterLayer;

  QDialogButtonBox *m_buttonBox;
};

#endif // DRAWINGSETTINGSDIALOG_H
