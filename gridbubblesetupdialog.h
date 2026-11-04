#ifndef GRIDBUBBLESETUPDIALOG_H
#define GRIDBUBBLESETUPDIALOG_H

#include <QDialog>
#include <QStringList>

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QRadioButton;
class QButtonGroup;
class LayerManager;

// 주열번호 설정 다이얼로그
// ┌ 라벨 선택 ───────────────────────────────┐
// │  라디오버튼 (X/Y/A/1)                    │
// │  라벨 크기 : [스핀박스]                   │
// │  라벨레이어 : [콤보]                      │
// │  □ 기울기 적용                            │
// └──────────────────────────────────────────┘
// ┌ 원 ──────────────────────────────────────┐
// │  원 지름 : [스핀박스]                     │
// │  원 레이어 : [콤보]                       │
// └──────────────────────────────────────────┘
class GridBubbleSetupDialog : public QDialog {
    Q_OBJECT

public:
    enum LabelType { TypeX = 0, TypeY, TypeA, TypeNum };

    // labelSize, diameter 는 기본값(스케일 미적용); 표시·저장·반환 모두 기본값 그대로
    explicit GridBubbleSetupDialog(LabelType      labelType,
                                   LayerManager  *layerMgr,
                                   const QString &currentLayer,
                                   const QString &currentLabelLayer,
                                   bool           tiltApply,
                                   double         labelSize,
                                   double         diameter,
                                   QWidget       *parent = nullptr);

    LabelType labelType()      const;
    QString   layerName()      const;   // 원 레이어
    QString   labelLayerName() const;   // 라벨 레이어
    bool      tiltEnabled()    const;
    double    labelSize()      const;   // 기본값 반환 (스케일 미적용)
    double    diameter()       const;   // 기본값 반환 (스케일 미적용)

private slots:
    void onOkClicked();
    void onCancelClicked();

private:
    void populateLayerCombo(QComboBox *combo, LayerManager *mgr,
                            const QString &selectedName);

    QButtonGroup  *m_btnGroup;
    QRadioButton  *m_radioX;
    QRadioButton  *m_radioY;
    QRadioButton  *m_radioA;
    QRadioButton  *m_radioNum;
    QDoubleSpinBox *m_spinLabelSize;   // 라벨 크기
    QComboBox     *m_comboLabelLayer;  // 라벨 레이어
    QCheckBox     *m_chkTilt;          // 기울기 적용
    QDoubleSpinBox *m_spinDiameter;    // 원 지름
    QComboBox     *m_comboLayer;       // 원 레이어
    QPushButton   *m_btnOk;
    QPushButton   *m_btnCancel;
};

#endif // GRIDBUBBLESETUPDIALOG_H
