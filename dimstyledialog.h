#ifndef DIMSTYLEDIALOG_H
#define DIMSTYLEDIALOG_H

#include "mydimension.h"
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QButtonGroup>

class IAppContext;

class DimStyleDialog : public QDialog {
    Q_OBJECT
public:
    explicit DimStyleDialog(IAppContext *ctx, QWidget *parent = nullptr);

    DimensionStyle getStyle() const;
    void setStyle(const DimensionStyle &style);

    // 치수 스케일 값 설정 (픽 완료 후 외부에서 호출)
    void setDimScale(double scale);

signals:
    void pickDimScaleRequested(); // 마우스 픽 요청 시그널

private slots:
    void onDimScalePresetChanged(int index);

private:
    IAppContext    *m_ctx;

    // 치수정보
    QDoubleSpinBox *m_baselineSpacing;
    QDoubleSpinBox *m_extOffset;
    QDoubleSpinBox *m_extExtend;

    // 화살표
    QDoubleSpinBox *m_arrowSize;
    QButtonGroup   *m_arrowStyleGroup;

    // 글자 정보
    QDoubleSpinBox *m_textSize;
    QDoubleSpinBox *m_toleranceSize;
    QDoubleSpinBox *m_textGap;
    QSpinBox       *m_precision;
    QSpinBox       *m_tolPrecision;
    QCheckBox      *m_suppressZero;
    QCheckBox      *m_suppressTolZero;
    QCheckBox      *m_useThousandSep;

    // 치수선 스케일
    QLineEdit      *m_dimScaleEdit;
    QComboBox      *m_comboDimScale;
    QPushButton    *m_btnPickDimScale;

    // 레이어 정보
    QComboBox      *m_dimLineLayer;
    QComboBox      *m_dimTextLayer;

    void setupUi();
    void syncLayers();
};

#endif // DIMSTYLEDIALOG_H
