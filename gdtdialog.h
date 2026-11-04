#ifndef GDTDIALOG_H
#define GDTDIALOG_H

#include "mygdt.h"

#include <QDialog>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QButtonGroup>
#include <QLabel>

class GdtPreviewWidget;
class QListWidget;
class QListWidgetItem;

class GdtDialog : public QDialog {
    Q_OBJECT
public:
    explicit GdtDialog(QWidget *parent = nullptr);

    // 사용자가 설정한 데이터 반환
    GdtData resultData() const;

    // 초기값 설정 (재편집 시)
    void setData(const GdtData &data);

    // [3-3] 도면이 기억한 "활성 프리셋"을 다이얼로그 열 때 다시 선택해 준다.
    //   name 은 확장자 뺀 파일명(basename). 목록에 없으면 조용히 무시.
    void preselectPreset(const QString &name);
    // 사용자가 마지막으로 선택(클릭)한 프리셋 basename. 없으면 빈 문자열.
    QString activePresetName() const { return m_activePreset; }

private slots:
    void onSymbolSelected(int id);
    void onInputChanged();
    void onSaveClicked();
    void onListItemClicked(QListWidgetItem *item);
    void onListContextMenu(const QPoint &pos);

private:
    void setupUI();
    void updatePreview();
    void refreshFileList(); // ./data 폴더의 저장된 프리셋 목록 갱신

    // 저장된 프리셋 목록 (왼쪽)
    QListWidget     *m_fileList;
    QPushButton     *m_btnSaveList;

    // 기호 선택 버튼 그룹
    QButtonGroup    *m_symGroup;

    // 공차 입력
    QCheckBox       *m_chkDiam;
    QLineEdit       *m_edTolerance;

    // 데이텀 입력 (최대 3개)
    QLineEdit       *m_edDatum[3];

    // 텍스트 높이
    QDoubleSpinBox  *m_spnTextHeight;
    QCheckBox       *m_chkUseLeader; // 지시선 체크박스

    // 미리보기
    GdtPreviewWidget *m_preview;

    // OK / Cancel
    QPushButton     *m_btnOk;
    QPushButton     *m_btnCancel;

    GdtSymbol        m_selectedSymbol;
    QString          m_activePreset; // [3-3] 마지막으로 클릭한 프리셋 basename
};

#endif // GDTDIALOG_H
