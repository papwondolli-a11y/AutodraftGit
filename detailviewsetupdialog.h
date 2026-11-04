#ifndef DETAILVIEWSETUPDIALOG_H
#define DETAILVIEWSETUPDIALOG_H

#include <QColor>
#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QComboBox;
class QDoubleSpinBox;
class QRadioButton;
class QPushButton;

// ─────────────────────────────────────────────────────────────────────────────
// DetailViewSetupDialog — 상세도(DETAIL) Setup 다이얼로그
//
//  레이어        : 상세도 엔티티가 놓일 레이어 (색상아이콘 + 이름)
//  라벨문자크기  : 참조 라벨(A,B,C…) 글자 높이 (초기값 = 도면 설정값)
//  라벨문자위치  : ◉ 지시선  — 경계원에서 지시선을 빼 그 끝에 라벨
//                  ○ 경계선  — 경계원 선상에 라벨을 놓고 그 자리를 끊음  (택1)
//  상세도 축척   : "2:1", "1:2" 형식 (편집 가능 콤보)
// ─────────────────────────────────────────────────────────────────────────────
class DetailViewSetupDialog : public QDialog {
    Q_OBJECT
public:
    DetailViewSetupDialog(const QList<QPair<QString, QColor>> &layers,
                          const QString &currentLayer, double labelTextSize,
                          bool labelOnLeader, bool labelOnBoundary,
                          const QString &scaleText, QWidget *parent = nullptr);

    QString layerName() const;
    double  labelTextSize() const;
    bool    labelOnLeader() const;
    bool    labelOnBoundary() const;
    QString scaleText() const;

private slots:
    void onOkClicked();
    void onCancelClicked();

private:
    QComboBox      *m_comboLayer;
    QDoubleSpinBox *m_spinLabelSize;
    QRadioButton   *m_radioLeader;
    QRadioButton   *m_radioBoundary;
    QComboBox      *m_comboScale;
    QPushButton    *m_btnOk;
    QPushButton    *m_btnCancel;
};

#endif // DETAILVIEWSETUPDIALOG_H
