#ifndef BOXTEXTSETUPDIALOG_H
#define BOXTEXTSETUPDIALOG_H

#include <QColor>
#include <QDialog>
#include <QList>
#include <QPair>
#include <QString>

class QCheckBox;
class QComboBox;
class QPushButton;

// ─────────────────────────────────────────────────────────────────────────────
// BoxTextSetupDialog — 박스글자(BOXTEXT) Setup 다이얼로그
//
//  박스 레이어   : 박스(폴리선)가 놓일 레이어 (색상아이콘 + 이름)
//  글자 레이어   : 글자가 놓일 레이어 (색상아이콘 + 이름)
//  ☑ 기존 글자 레이어 적용 : Pick 모드에서 선택한 기존 글자를 글자 레이어로 변경 (초기값: 체크)
//  ☑ 박스크기 연동 : 글자 수정 시 박스 크기 자동 변경 (초기값: 체크)
// ─────────────────────────────────────────────────────────────────────────────
class BoxTextSetupDialog : public QDialog {
    Q_OBJECT
public:
    BoxTextSetupDialog(const QList<QPair<QString, QColor>> &layers,
                       const QString &boxLayer, const QString &textLayer,
                       bool applyTextLayerToPicked, bool linkBoxSize,
                       QWidget *parent = nullptr);

    QString boxLayer() const;
    QString textLayer() const;
    bool    applyTextLayerToPicked() const;
    bool    linkBoxSize() const;

private slots:
    void onOkClicked();
    void onCancelClicked();

private:
    QComboBox *createLayerCombo(const QList<QPair<QString, QColor>> &layers,
                                const QString &current);

    QComboBox   *m_comboBoxLayer;
    QComboBox   *m_comboTextLayer;
    QCheckBox   *m_chkApplyTextLayer;
    QCheckBox   *m_chkLinkBoxSize;
    QPushButton *m_btnOk;
    QPushButton *m_btnCancel;
};

#endif // BOXTEXTSETUPDIALOG_H
