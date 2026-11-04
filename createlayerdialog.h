#ifndef CREATELAYERDIALOG_H
#define CREATELAYERDIALOG_H

#include "layercomboboxmanager.h"
#include <QDialog>
#include <QVBoxLayout>

struct LayerData {
    QString layerName;
    QString lineType;
    QString lineWidth;
    QString color;
};

class QLineEdit;
class QDialogButtonBox;
class QPushButton;
class QRadioButton;
class ColorComboBox;
class AppContext;


class CreateLayerDialog : public QDialog {
    Q_OBJECT
public:
    //explicit CreateLayerDialog(QWidget *parent = nullptr);

    explicit CreateLayerDialog(AppContext *context, QWidget *parent = nullptr);
    ~CreateLayerDialog();

    // 입력된 값 반환 함수들
    QString layerName() const;
    QString layerColor() const;
    QString lineType() const;
    QString lineWeight() const;
    QIcon colorIcon() const;

private slots:
    void onSelectColor();
    void onAccept();
    // 라인타입 그룹 라디오(AutoDraft / AutoCAD호환(S) / AutoCAD호환(ISO)) 변경 시 콤보 재구성
    void onLinetypeGroupChanged();

private:
    QLineEdit *m_nameEdit;
    QLineEdit *m_colorEdit;
    QPushButton *m_colorButton;
    QLineEdit *m_lineTypeEdit;
    QLineEdit *m_lineWeightEdit;
    QDialogButtonBox *m_buttonBox;
    ColorComboBox *m_colorComboBox;
    QWidget *m_centralWidget;
    QVBoxLayout *m_mainLayout;
    LineTypeComboBox *linetypeCombo;
    QRadioButton *m_rbAutoDraft;
    QRadioButton *m_rbAcadStd;
    QRadioButton *m_rbAcadIso;
    LineWidthComboBox *linewidthCombo;
    ColorComboBox *colorCombo;
    QPushButton* m_okButton;
    QPushButton* m_cancelButton;
    AppContext *m_appContext;

};

#endif // CREATELAYERDIALOG_H
