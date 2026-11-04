#ifndef TWOLINESETUPDIALOG_H
#define TWOLINESETUPDIALOG_H

#include <QDialog>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class LayerManager;

class TwoLineSetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit TwoLineSetupDialog(double currentSpacing,
                                const QString &currentLayerName,
                                bool centerLineEnabled,
                                bool showMortar,
                                double mortarSpacingVal,
                                const QString &mortarLayerName,
                                bool showSpace,
                                double spaceSpacingVal,
                                const QString &spaceLayerName,
                                bool showEccentric,
                                double eccentricOffsetVal,
                                // 편심6-Line 전용 파라미터 (showSpace && showEccentric 일 때만 사용)
                                double innerWallThicknessVal,
                                double spaceThicknessVal,
                                double outerWallThicknessVal,
                                bool innerMortarEnabledVal,
                                bool outerMortarEnabledVal,
                                LayerManager *layerMgr,
                                QWidget *parent = nullptr);

    // ── 일반 게터 ──────────────────────────────────────────────────────────────
    double  spacing() const;           // showEccentric6 시 0.0 반환 (미사용)
    QString selectedLayerName() const;
    bool    centerLineEnabled() const;
    double  mortarSpacing() const;     // showMortar=false 시 0.0 반환
    QString mortarLayerName() const;   // showMortar=false 시 "" 반환
    double  spaceSpacing() const;      // showSpace=false 또는 isEccentric6 시 0.0 반환
    QString spaceLayerName() const;    // showSpace=false 또는 isEccentric6 시 "" 반환
    double  eccentricOffset() const;   // showEccentric=false 시 0.0 반환

    // ── 편심6-Line 전용 게터 ────────────────────────────────────────────────────
    double innerWallThickness() const; // 안쪩 (isEccentric6 아닐 시 0.0)
    double spaceThickness() const;     // 공간 (isEccentric6 아닐 시 0.0)
    double outerWallThickness() const; // 바깥쪩 (isEccentric6 아닐 시 0.0)
    bool   innerMortarEnabled() const; // 내벽 몰타르 활성 (isEccentric6 아닐 시 true)
    bool   outerMortarEnabled() const; // 외벽 몰타르 활성 (isEccentric6 아닐 시 true)

private:
    void populateCombo(QComboBox *combo, LayerManager *mgr,
                       const QString &selectedName);

    // ── 공통 위젯 ──────────────────────────────────────────────────────────────
    QDoubleSpinBox *m_spinBox             = nullptr;  // 일반 모드 "두 선 간격" (isEccentric6 시 미생성)
    QComboBox      *m_layerCombo          = nullptr;  // 외벽/선간격 설정 레이어
    QCheckBox      *m_centerLineCheck     = nullptr;

    // ── 몰타르선 위젯 (showMortar=true 시 생성) ──────────────────────────────
    QDoubleSpinBox *m_mortarSpinBox       = nullptr;
    QComboBox      *m_mortarLayerCombo    = nullptr;
    // 편심6-Line 전용 몰타르 체크박스
    QCheckBox      *m_innerMortarCheck    = nullptr;
    QCheckBox      *m_outerMortarCheck    = nullptr;

    // ── 공간선 위젯 (showSpace=true && !isEccentric6 시 생성) ─────────────────
    QDoubleSpinBox *m_spaceSpinBox        = nullptr;
    QComboBox      *m_spaceLayerCombo     = nullptr;

    // ── 편심 설정 위젯 (showEccentric=true 시 생성) ───────────────────────────
    QDoubleSpinBox *m_eccentricSpinBox    = nullptr;

    // ── 편심6-Line 전용 선간격 위젯 (isEccentric6 시 생성) ────────────────────
    QDoubleSpinBox *m_innerWallSpinBox    = nullptr;  // 안쪩
    QDoubleSpinBox *m_spaceThickSpinBox   = nullptr;  // 공간
    QDoubleSpinBox *m_outerWallSpinBox    = nullptr;  // 바깥쪩
};

#endif // TWOLINESETUPDIALOG_H
