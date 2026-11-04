#ifndef PLANDOORDOUBLESETUPDIALOG_H
#define PLANDOORDOUBLESETUPDIALOG_H

#include <QDialog>
#include <QStringList>

class QComboBox;
class SpaceFinishGroupBox;
class QDoubleSpinBox;
class QPushButton;
class QRadioButton;

// ─────────────────────────────────────────────────────────────────────────────
// PlanDoorDoubleSetupDialog — 쌍여닫이(양개문) 설정 다이얼로그
//
// 좌우 문짝 너비를 각각 별도로 입력받는다. 시작점(P1)이 있는 쪽이 문1이다.
//
// [문 크기]
//   문1 너비 (시작점 P1측), 문2 너비  — 각각 기본값 900mm (합 1800mm)
//   V: 문틀 두께  +  V-모드 라디오
//       ○ 입력값 (초기 활성): V 스핀박스로 수동 설정
//       ○ 벽 두께 값        : P1→P2 클릭 거리에서 자동 계산
// [문틀 형태]  Type 1~4 썸네일 (PlanDoorTool 과 동일 단면)
// [Layer]      도어 레이어설정 버튼(DoorLayerDialog)에서 공통 관리
// ─────────────────────────────────────────────────────────────────────────────
class PlanDoorDoubleSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit PlanDoorDoubleSetupDialog(double            h1,
                                       double            h2,
                                       double            v,
                                       bool              useWallThickness,
                                       int               frameType,
                                       QWidget *parent = nullptr);

    double  h1()               const;
    double  h2()               const;
    double  v()                const;
    bool    useWallThickness() const;
    int     frameType()        const;
    void    setSpaceFinishType(int type);
    int     spaceFinishType() const;

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onVModeInputToggled(bool checked);

private:
    QDoubleSpinBox *m_spinH1;
    QDoubleSpinBox *m_spinH2;
    QDoubleSpinBox *m_spinV;
    QRadioButton   *m_radioVInput;
    QRadioButton   *m_radioVWall;
    QRadioButton   *m_radioFrameType[4];
    SpaceFinishGroupBox *m_spaceFinishGroup;
    QPushButton    *m_btnOk;
    QPushButton    *m_btnCancel;
};

#endif // PLANDOORDOUBLESETUPDIALOG_H
