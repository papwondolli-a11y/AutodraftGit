#ifndef HOLEDIALOG_H
#define HOLEDIALOG_H

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

// 구멍 설정창 [확인] 시 입력 검사 (구멍R HoleDialog · 구멍P HolePDialog 공용)
//   켜져 있는 항목의 깊이(DP)·지름이 0 이거나 맞지 않으면 문제 문구와 고칠 칸(field)을 돌려준다.
class HoleInputCheck {
public:
    enum Field { None = 0, BaseDepth, ThreadDepth, CbDiameter, CbDepth, CsDiameter };
    // 문제가 없으면 빈 문자열
    static QString problem(const MyHole &hole, Field &field);
};

// 구멍(드릴/탭/카운터보어/카운터싱크) 스펙 입력 다이얼로그
class HoleDialog : public QDialog {
    Q_OBJECT
public:
    explicit HoleDialog(LayerManager *layerMgr, QWidget *parent = nullptr);

    MyHole resultData() const;
    void setData(const MyHole &data);

    // [확인]: 켜진 항목의 깊이·지름이 0 이면 오류 메시지를 띄우고 그 칸으로 돌아가 다시 입력받는다
    void accept() override;

    // 코너(두 기준선 교차점) 기준 배치용 오프셋. 0,0이면 일반 클릭 배치.
    double offsetX() const { return m_spnOffsetX->value(); }
    double offsetY() const { return m_spnOffsetY->value(); }

    // 배열 배치. X/Y 갯수가 둘 다 1이면 단일 배치.
    int arrayCountX() const { return m_spnArrayCountX->value(); }
    double arraySpacingX() const { return m_spnArraySpacingX->value(); }
    int arrayCountY() const { return m_spnArrayCountY->value(); }
    double arraySpacingY() const { return m_spnArraySpacingY->value(); }

private slots:
    void onThreadToggled(bool checked);
    void onCounterboreToggled(bool checked);
    void onCountersinkToggled(bool checked);
    void onThreadSpecChanged(const QString &text);
    void onBaseDiameterChanged(double value);
    void onBaseThroughToggled(bool throughChecked);
    void onBaseDepthChanged(double value);
    // [신규] 전체/오른쪽/왼쪽 표시 범위 체크박스 3개를 라디오처럼 상호배타적으로 동작시킴
    // (항상 하나는 켜져 있어야 함 — 셋 다 꺼진 상태를 막는다)
    void onRenderModeToggled(bool checked);

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
    QCheckBox      *m_chkRenderFull;
    QCheckBox      *m_chkRenderRight;
    QCheckBox      *m_chkRenderLeft;
    QDoubleSpinBox *m_spnOffsetX;
    QDoubleSpinBox *m_spnOffsetY;
    QSpinBox       *m_spnArrayCountX;
    QDoubleSpinBox *m_spnArraySpacingX;
    QSpinBox       *m_spnArrayCountY;
    QDoubleSpinBox *m_spnArraySpacingY;

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

#endif // HOLEDIALOG_H
