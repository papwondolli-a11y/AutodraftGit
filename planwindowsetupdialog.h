#ifndef PLANWINDOWSETUPDIALOG_H
#define PLANWINDOWSETUPDIALOG_H

#include <QDialog>
#include <QColor>
#include <QList>
#include <QPair>
#include <QString>

class QCheckBox;
class QComboBox;
class SpaceFinishGroupBox;
class QDoubleSpinBox;
class QPushButton;
class QRadioButton;

// ─────────────────────────────────────────────────────────────────────────────
// PlanWindowSetupDialog — 평면 창문 설정 다이얼로그
//   Layer·창 상세치수(Win1~7)는 "창문설정" 버튼(WindowSettingsDialog)에서 공통 관리
// ─────────────────────────────────────────────────────────────────────────────
class PlanWindowSetupDialog : public QDialog {
    Q_OBJECT

public:
    explicit PlanWindowSetupDialog(double            w,
                                   bool              glass,
                                   bool              isDouble,
                                   double            sashW,
                                   int               sashes,
                                   bool              topOnly = false,
                                   QWidget *parent = nullptr);

    double  w()                const;
    void    setSpaceFinishType(int type);
    int     spaceFinishType() const;
    bool    glass()            const;
    double  sashW()            const;   // 슬라이딩 창문(짝1) 넓이, 0 = 같은 크기
    bool    isDouble()         const;   // true = 이중창, false = 단창

private slots:
    void onOkClicked();
    void onCancelClicked();

private:
    QDoubleSpinBox *m_spinW;
    QCheckBox      *m_chkGlass;
    QDoubleSpinBox *m_spinSashW;
    QRadioButton   *m_radioSingle;
    QRadioButton   *m_radioDouble;
    SpaceFinishGroupBox *m_spaceFinishGroup;
    QPushButton    *m_btnOk;
    QPushButton    *m_btnCancel;
};

#endif // PLANWINDOWSETUPDIALOG_H
