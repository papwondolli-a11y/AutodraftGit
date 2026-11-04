#ifndef PLANDOORSETUPDIALOG_H
#define PLANDOORSETUPDIALOG_H

#include <QDialog>
#include <QPixmap>
#include <QStringList>

class QComboBox;
class QDoubleSpinBox;
class QGroupBox;
class SpaceFinishGroupBox;
class QPushButton;
class QRadioButton;

// ─────────────────────────────────────────────────────────────────────────────
// PlanDoorSetupDialog — 평면도어 설정 다이얼로그
//
// [문 크기]
//   H: 문 너비
//   V: 문틀 두께  +  V-모드 라디오
//       ○ 입력값 (초기 활성): V 스핀박스로 수동 설정
//       ○ 벽 두께 값        : P1→P2 클릭 거리에서 자동 계산
// [문틀 형태]  Type 1~4 썸네일
// [공간벽 마감형태] swtype1~4 아이콘 (:/icons/swtype1~4.png)
// [Layer]      도어 레이어설정 버튼(DoorLayerDialog)에서 공통 관리
// ─────────────────────────────────────────────────────────────────────────────
// 문틀 형태(Type 1~4) 단면 미리보기 아이콘 (설정 다이얼로그·속성창 공용)
QPixmap makeJambIcon(int type, int w, int h);

class PlanDoorSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit PlanDoorSetupDialog(double            h,
                                 double            v,
                                 bool              useWallThickness,
                                 int               frameType,
                                 QWidget *parent = nullptr);

    double  h()                const;
    double  v()                const;
    bool    useWallThickness() const;
    int     frameType()        const;
    void    setSpaceFinishType(int type);
    int     spaceFinishType()  const;

private slots:
    void onOkClicked();
    void onCancelClicked();
    void onVModeInputToggled(bool checked);

private:
    QDoubleSpinBox *m_spinH;
    QDoubleSpinBox *m_spinV;
    QRadioButton   *m_radioVInput;
    QRadioButton   *m_radioVWall;
    QRadioButton   *m_radioFrameType[4];
    SpaceFinishGroupBox *m_spaceFinishGroup;
    QPushButton    *m_btnOk;
    QPushButton    *m_btnCancel;
};

#endif // PLANDOORSETUPDIALOG_H
