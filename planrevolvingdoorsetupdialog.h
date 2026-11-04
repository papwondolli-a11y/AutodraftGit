#ifndef PLANREVOLVINGDOORSETUPDIALOG_H
#define PLANREVOLVINGDOORSETUPDIALOG_H

#include <QDialog>
#include <QStringList>

class QComboBox;
class SpaceFinishGroupBox;
class QDoubleSpinBox;
class QPushButton;
class QRadioButton;

// ─────────────────────────────────────────────────────────────────────────────
// PlanRevolvingDoorSetupDialog — 회전문 설정 다이얼로그
//
// [문 크기]
//   H: 문 크기(개구부 전체 폭)
//   V: 문틀 두께(=벽 두께)  +  V-모드 라디오(입력값 / 벽 두께 값)
// [날개 개수]  3개 / 4개 라디오
// [Layer]      도어 레이어설정 버튼(DoorLayerDialog)에서 공통 관리
//
// 회전문 문틀은 항상 Type4(단순 사각) 고정이라 프레임 형태 선택 UI는 없다.
// ─────────────────────────────────────────────────────────────────────────────
class PlanRevolvingDoorSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit PlanRevolvingDoorSetupDialog(double            h,
                                          double            v,
                                          bool              useWallThickness,
                                          int               wingCount,
                                          QWidget *parent = nullptr);

    double  h()                const;
    double  v()                const;
    bool    useWallThickness() const;
    int     wingCount()        const;
    void    setSpaceFinishType(int type);
    int     spaceFinishType() const;

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onVModeInputToggled(bool checked);

private:
    QDoubleSpinBox *m_spinH;
    QDoubleSpinBox *m_spinV;
    QRadioButton   *m_radioVInput;
    QRadioButton   *m_radioVWall;
    QRadioButton   *m_radioWing3;
    QRadioButton   *m_radioWing4;
    SpaceFinishGroupBox *m_spaceFinishGroup;
    QPushButton    *m_btnOk;
    QPushButton    *m_btnCancel;
};

#endif // PLANREVOLVINGDOORSETUPDIALOG_H
