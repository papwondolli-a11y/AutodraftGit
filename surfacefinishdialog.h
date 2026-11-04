#ifndef SURFACEFINISHDIALOG_H
#define SURFACEFINISHDIALOG_H

#include "mysurfacefinish.h"

#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QStackedWidget>
#include <QLabel>
#include <QIcon>

class IAppContext;

class SurfaceFinishDialog : public QDialog {
    Q_OBJECT
public:
    explicit SurfaceFinishDialog(IAppContext *ctx = nullptr, QWidget *parent = nullptr);

    SurfaceFinishData resultData() const;
    void setData(const SurfaceFinishData &data);

private slots:
    void onModeChanged();
    void onIsoSymbolSelected(int id);
    void onLegacyGradeSelected(int id);
    void onRaModeChanged(int id);

private:
    void setupUI();
    void populateLayerCombo(QComboBox *combo);
    static QIcon renderSymbolIcon(SurfaceFinishSymbol sym, const QSize &size);

    IAppContext *m_ctx = nullptr;

    QButtonGroup *m_modeGroup;    // 신기호 / 구기호
    QButtonGroup *m_isoSymGroup;  // 기본 / 삼각 기호 / 제거가공금지
    QButtonGroup *m_legacyGroup;  // 거친/중/상/정밀다듬질
    QButtonGroup *m_raModeGroup;  // RA(숫자값) / W / X / Y / Z(약식 등급 문자)

    QStackedWidget *m_stack;

    QComboBox    *m_edRa; // 편집 가능한 콤보박스 (표준 계열값 선택 또는 직접 입력)
    QLineEdit    *m_edMethod;
    QCheckBox    *m_chkTailBar; // 꼬리선(긴 다리 끝 막대) 추가 여부

    QDoubleSpinBox *m_spnTextHeight;
    QCheckBox      *m_chkUseLeader;

    QComboBox *m_comboSymbolLayer; // 기호 전용 레이어 (색상 아이콘 포함)
    QComboBox *m_comboTextLayer;   // 문자 전용 레이어 (색상 아이콘 포함)

    SurfaceFinishSymbol m_isoSymbol = SurfaceFinishSymbol::Basic;
    int m_legacyGrade = 2;
    bool m_legacyMode = false;
    QString m_raLetterCode; // 빈 문자열이면 RA(숫자 값) 모드
};

#endif // SURFACEFINISHDIALOG_H
