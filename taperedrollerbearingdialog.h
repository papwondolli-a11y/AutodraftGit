#ifndef TAPEREDROLLERBEARINGDIALOG_H
#define TAPEREDROLLERBEARINGDIALOG_H

#include "taperedrollerbearingdata.h"

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;

// ─────────────────────────────────────────────────────────────────────────
// TaperedRollerBearingDialog — 계열·호칭번호를 고르면 d/D/B/T/C/r/R 이 채워지고 직접 수정할 수도 있다.
// ─────────────────────────────────────────────────────────────────────────
class TaperedRollerBearingDialog : public QDialog {
    Q_OBJECT
public:
    explicit TaperedRollerBearingDialog(QWidget *parent = nullptr);

    void setParams(const TaperedRollerBearingParams &params);
    TaperedRollerBearingParams params() const;

private slots:
    void onFamilyChanged(int index);
    void onSeriesChanged(int index);
    void updateAutoHatchScale();

private:
    void setupUI();

    QComboBox      *m_family;   // 계열 (320/302/322/303/323)
    QComboBox      *m_series;   // 호칭번호
    QDoubleSpinBox *m_d;
    QDoubleSpinBox *m_D;
    QDoubleSpinBox *m_B;
    QDoubleSpinBox *m_T;
    QDoubleSpinBox *m_C;
    QDoubleSpinBox *m_r;
    QDoubleSpinBox *m_R;
    QDoubleSpinBox *m_angle;
    QCheckBox      *m_dims;
    QCheckBox      *m_center;
    QCheckBox      *m_hatch;
    QDoubleSpinBox *m_hatchScale;
    QCheckBox      *m_full;
};

#endif // TAPEREDROLLERBEARINGDIALOG_H
