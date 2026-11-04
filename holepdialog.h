#ifndef HOLEPDIALOG_H
#define HOLEPDIALOG_H

#include "myhole.h"

#include <QDialog>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QRadioButton>
#include <QSpinBox>

class LayerManager;

// 구멍(드릴/탭/카운터보어/카운터싱크) 스펙 입력 다이얼로그 - 원/호 중심 기준 원형배열 버전(구멍 P)
class HolePDialog : public QDialog {
    Q_OBJECT
public:
    explicit HolePDialog(LayerManager *layerMgr, QWidget *parent = nullptr);

    MyHole resultData() const;
    void setData(const MyHole &data);

    // 중심점(클릭한 점 또는 선택한 두 원/호의 중앙)으로부터의 거리. 0이면 중심에 그대로 배치.
    double centerDistance() const { return m_spnCenterDistance->value(); }

    // 원형배열. 갯수가 1이면 단일 배치(범위는 무시).
    double arrayRange() const { return m_spnArrayRange->value(); }
    int arrayCount() const { return m_spnArrayCount->value(); }

private slots:
    void onThreadToggled(bool checked);
    void onCounterboreToggled(bool checked);
    void onCountersinkToggled(bool checked);
    void onThreadSpecChanged(const QString &text);
    void onBaseDiameterChanged(double value);
    void onBaseThroughToggled(bool throughChecked);
    void onBaseDepthChanged(double value);

private:
    void setupUI();
    void populateLayerCombo(QComboBox *combo, const QString &selectedName);
    void applyTapDrillDiameter(const QString &spec);
    void applyApproxCbCsDiameter(double baseDiameter);
    void updateThreadDepthConstraint();

    LayerManager *m_layerMgr;

    // 기본 구멍
    QDoubleSpinBox *m_spnDiameter;
    QRadioButton   *m_radioThrough;
    QRadioButton   *m_radioBlind;
    QDoubleSpinBox *m_spnDepth;
    QComboBox      *m_cmbBaseLayer;
    QCheckBox      *m_chkCenterLine;
    QDoubleSpinBox *m_spnCenterDistance;
    QDoubleSpinBox *m_spnArrayRange;
    QSpinBox       *m_spnArrayCount;

    // 탭(나사)
    QCheckBox      *m_chkThread;
    QComboBox      *m_cmbThreadSpec;
    QLineEdit      *m_edThreadClass;
    QLabel         *m_lblThreadDepth;
    QDoubleSpinBox *m_spnThreadDepth;
    QComboBox      *m_cmbThreadLayer;

    // 공차등급 (리머/정밀구멍)
    QLineEdit      *m_edTolerance;

    // 카운터보어
    QCheckBox      *m_chkCounterbore;
    QDoubleSpinBox *m_spnCbDiameter;
    QDoubleSpinBox *m_spnCbDepth;
    QComboBox      *m_cmbCbLayer;

    // 카운터싱크
    QCheckBox      *m_chkCountersink;
    QDoubleSpinBox *m_spnCsDiameter;
    QDoubleSpinBox *m_spnCsAngle;
    QComboBox      *m_cmbCsLayer;

    // 콜아웃 표기 방식
    QRadioButton   *m_radioStyleText;
    QRadioButton   *m_radioStyleGraphic;

    QPushButton    *m_btnOk;
    QPushButton    *m_btnCancel;
};

#endif // HOLEPDIALOG_H
