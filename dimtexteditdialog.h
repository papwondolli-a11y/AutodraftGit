#ifndef DIMTEXTEDITDIALOG_H
#define DIMTEXTEDITDIALOG_H

#include <QDialog>
#include <QLineEdit>
#include <QComboBox>
#include <QRadioButton>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QGroupBox>
#include <QCheckBox> // [신규]

struct DimTextEditData {
    QString content;
    QString prefix;
    QString suffix;
    int tolType;
    double tolUpper;
    double tolLower;
    QString fitCode;      // [신규] 끼워맞춤 등급기호 (ex: "H7") - tolType==4일 때 사용
    bool fitShowValues = false; // [신규] 등급기호 옆에 숫자 상/하한도 병기할지 여부 (기본 false, 끼워맞춤 모드가 아닐 때도 항상 초기화되도록 기본값 지정)
    int tolPrecision;
    bool tolUseAngle;    // 각도 부호(°) 사용 여부
    bool syncGeometry;   // 본문 숫자값으로 치수선 geometry도 갱신할지 여부
};

class DimTextEditDialog : public QDialog {
    Q_OBJECT
public:
    explicit DimTextEditDialog(const DimTextEditData &initialData, QWidget *parent = nullptr);
    DimTextEditData getData() const;

private slots:
    void onTolTypeChanged();
    void onPrefixIndexChanged(int index);
    void onFitCodeTextChanged(const QString &text); // [신규] 등급기호 선택/입력 시 꼬리말에 동기화

private:
    void setupUi();
    void loadData(const DimTextEditData &data);

    QLineEdit *m_contentEdit;
    QComboBox *m_prefixCombo;
    QLineEdit *m_suffixEdit;

    QRadioButton *m_radioNone;
    QRadioButton *m_radioSym;
    QRadioButton *m_radioDev;
    QRadioButton *m_radioFit;   // [신규] 끼워맞춤(FitCode) 라디오버튼

    QGroupBox *m_symGroup;
    QDoubleSpinBox *m_symValue;
    QCheckBox *m_tolAngleCheck;  // 각도 체크박스
    QCheckBox *m_syncGeomCheck;  // 치수선 geometry 연동 체크박스

    QGroupBox *m_devGroup;
    QDoubleSpinBox *m_devUpper;
    QDoubleSpinBox *m_devLower;

    QGroupBox *m_fitGroup;         // [신규] 끼워맞춤 그룹박스
    QComboBox *m_fitCodeCombo;     // [신규] 끼워맞춤 등급기호 입력(콤보+직접입력)
    QCheckBox *m_fitShowValuesCheck; // [신규] 숫자 상/하한 병기 체크박스

    QSpinBox *m_tolPrecision;
};

#endif // DIMTEXTEDITDIALOG_H