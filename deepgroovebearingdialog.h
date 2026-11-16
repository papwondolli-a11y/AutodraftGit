#ifndef DEEPGROOVEBEARINGDIALOG_H
#define DEEPGROOVEBEARINGDIALOG_H

#include "deepgroovebearingdata.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

// ─────────────────────────────────────────────────────────────────────────
// DeepGrooveBearingDialog — 호칭번호를 고르면 d/D/B/r 이 채워지고, 값은 직접 고칠 수도 있다.
// ─────────────────────────────────────────────────────────────────────────
class DeepGrooveBearingDialog : public QDialog {
    Q_OBJECT
public:
    explicit DeepGrooveBearingDialog(QWidget *parent = nullptr);

    void setParams(const DeepGrooveBearingParams &params);
    DeepGrooveBearingParams params() const;

private slots:
    void onFamilyChanged(int index);
    void onSeriesChanged(int index);
    void updateAutoHatchScale();

private:
    void setupUI();

    QComboBox      *m_family;   // 계열 (60/62/63/64/68/69)
    QComboBox      *m_series;   // 호칭번호
    QDoubleSpinBox *m_d;
    QDoubleSpinBox *m_D;
    QDoubleSpinBox *m_B;
    QDoubleSpinBox *m_r;
    QCheckBox      *m_dims;
    QCheckBox      *m_center;
    QCheckBox      *m_hatch;
    QDoubleSpinBox *m_hatchScale;
    QCheckBox      *m_full;
};

#endif // DEEPGROOVEBEARINGDIALOG_H
