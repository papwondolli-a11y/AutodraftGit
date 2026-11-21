#ifndef THRUSTBEARINGDIALOG_H
#define THRUSTBEARINGDIALOG_H

#include "thrustbearingdata.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;

// ─────────────────────────────────────────────────────────────────────────
// ThrustBearingDialog — 종류(평면자리/조심자리/조심자리와셔) · 계열 · 호칭번호를 고르면
// d/de/D/H/c(+H1/H2)가 채워지고 직접 수정할 수도 있다.
// ─────────────────────────────────────────────────────────────────────────
class ThrustBearingDialog : public QDialog {
    Q_OBJECT
public:
    explicit ThrustBearingDialog(QWidget *parent = nullptr);

    void setParams(const ThrustBearingParams &params);
    ThrustBearingParams params() const;

private slots:
    void onModeChanged(int index);
    void onTypeChanged(int index);
    void onFamilyChanged(int index);
    void onSeriesChanged(int index);
    void onHChanged();
    void updateAutoHatchScale();

private:
    void setupUI();
    void refreshSeriesLabels();
    void updateTypeDependentUi();
    void repopulateFamilies(bool dbl);
    bool isDouble() const;
    QLabel *m_deLabel = nullptr;
    QLabel *m_diLabel = nullptr;
    QLabel *m_aLabel = nullptr;
    QLabel *m_c1Label = nullptr;

    QComboBox      *m_mode;     // 단식 / 복식
    QComboBox      *m_type;
    QComboBox      *m_family;
    QComboBox      *m_series;
    QDoubleSpinBox *m_d;
    QDoubleSpinBox *m_de;
    QDoubleSpinBox *m_D;
    QDoubleSpinBox *m_H;
    QDoubleSpinBox *m_c;
    QDoubleSpinBox *m_di;
    QDoubleSpinBox *m_a;
    QDoubleSpinBox *m_c1;
    QDoubleSpinBox *m_H1;
    QDoubleSpinBox *m_H2;
    QCheckBox      *m_dims;
    QCheckBox      *m_center;
    QCheckBox      *m_hatch;
    QDoubleSpinBox *m_hatchScale;
    QCheckBox      *m_full;
};

#endif // THRUSTBEARINGDIALOG_H
