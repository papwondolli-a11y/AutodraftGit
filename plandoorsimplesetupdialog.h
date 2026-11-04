#ifndef PLANDOORSIMPLESETUPDIALOG_H
#define PLANDOORSIMPLESETUPDIALOG_H

#include <QDialog>
#include <QStringList>

class QComboBox;
class SpaceFinishGroupBox;
class QDoubleSpinBox;
class QPushButton;

// ─────────────────────────────────────────────────────────────────────────────
// PlanDoorSimpleSetupDialog — 단순문 설정
//   [문 크기]  W: 문 너비 (벽 두께는 항상 P1·P2 측정값 사용)
//   [Layer]    도어 레이어설정 버튼(DoorLayerDialog)에서 공통 관리
// ─────────────────────────────────────────────────────────────────────────────
class PlanDoorSimpleSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit PlanDoorSimpleSetupDialog(double h,
                                       QWidget *parent = nullptr);

    double  h()                const;
    void    setSpaceFinishType(int type);
    int     spaceFinishType() const;

private:
    QDoubleSpinBox *m_spinH;
    SpaceFinishGroupBox *m_spaceFinishGroup;
    QPushButton    *m_btnOk;
    QPushButton    *m_btnCancel;
};

#endif // PLANDOORSIMPLESETUPDIALOG_H
