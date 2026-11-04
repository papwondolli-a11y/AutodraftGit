#ifndef STAIRPLANDIALOG_H
#define STAIRPLANDIALOG_H

#include "stairplantool.h"
#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QRadioButton;
class QSpinBox;
class QDoubleSpinBox;
class LayerManager;
class LineTypeManager;

// ─────────────────────────────────────────────────────────────────────────────
// StairPlanDialog — 계단평면 설정 (lisp/stair.dcl "Stair Drawing Tools" + "Stair Layer Control")
// ─────────────────────────────────────────────────────────────────────────────
class StairPlanDialog : public QDialog {
    Q_OBJECT

public:
    StairPlanDialog(const StairPlanParams &params, LayerManager *layerMgr,
                    LineTypeManager *ltMgr, double wallLength = 0.0,
                    QWidget *parent = nullptr);

    StairPlanParams params() const;

    // 레이어 콤보 채우기 (색 아이콘, 도면에 없는 기본 레이어는 항목 추가) — 잡석깔기 등에서 재사용
    static void populateLayerCombo(QComboBox *combo, LayerManager *mgr, const QString &selected);

private slots:
    void onOkClicked();
    void onTypeChanged();
    void onTypeToggled();
    void onLandEdited(double v);

private:

    void updateLandDisplay();

    QLabel         *m_labelImage;
    QRadioButton   *m_radioType1;
    QRadioButton   *m_radioType2;
    QSpinBox       *m_spinMid;
    double          m_landType1;
    double          m_wallLength;   // P1-P2 길이 (0 = 모름)
    QSpinBox       *m_spinUp;
    QSpinBox       *m_spinDown;
    QDoubleSpinBox *m_spinTread;
    QDoubleSpinBox *m_spinTreadLen;
    QDoubleSpinBox *m_spinLand;
    QRadioButton   *m_radioNumYes;
    QRadioButton   *m_radioNumNo;
    QRadioButton   *m_radioSlipYes;
    QRadioButton   *m_radioSlipNo;
    QComboBox      *m_comboLayer;
    QComboBox      *m_comboNonslipLayer;
};

#endif // STAIRPLANDIALOG_H
