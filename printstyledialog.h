#ifndef PRINTSTYLEDIALOG_H
#define PRINTSTYLEDIALOG_H

#include <QDialog>
#include <QTableWidget>
#include <QPushButton>
#include <QRadioButton>
#include <QVector>
#include <QColor>

class IAppContext;

// 레이어별로 "인쇄 전용 색상"을 지정/해제하는 다이얼로그 (출력 스타일 2단계).
// 지정하지 않은(자동) 레이어는 인쇄 시 화면 색상에 자동 규칙(밝은 색 -> 검정)이 적용된다
// (ScenePrinter::resolvePrintColor 참고). 여기서 지정한 색은 그 규칙보다 항상 우선한다.
//
// 상단의 "흑백/컬러" 라디오버튼은 테이블 전체에 한 번에 적용하는 일괄 조작이다:
// "흑백"을 고르면 모든 레이어의 출력 색상을 검정으로, "컬러"를 고르면 전부 "자동"으로 되돌린다.
class PrintStyleDialog : public QDialog {
    Q_OBJECT
public:
    explicit PrintStyleDialog(IAppContext *ctx, QWidget *parent = nullptr);

private slots:
    void onColorButtonClicked(); // "출력 색상" 버튼 - QColorDialog로 지정
    void onResetButtonClicked(); // "자동" 버튼 - 오버라이드 해제
    void onModeChanged();        // "흑백/컬러" 라디오버튼 - 전체 레이어에 일괄 적용
    void onAccept();             // 확인 - 편집 내용을 실제 Layer에 반영 후 accept()

private:
    void setupUI();
    void populateTable();
    void updateRowButton(int row); // 출력 색상 버튼의 배경색/텍스트를 편집 상태에 맞게 갱신

    IAppContext *m_ctx;
    QRadioButton *m_radioBlackWhite; // 전체 레이어 출력 색상을 검정으로 일괄 지정
    QRadioButton *m_radioColor;      // 전체 레이어 출력 색상을 자동으로 일괄 초기화
    QTableWidget *m_table;
    QPushButton *m_btnOk;
    QPushButton *m_btnCancel;

    // 편집 중인 상태 (행 인덱스 -> 지정 색상). invalid QColor = "자동(오버라이드 없음)".
    // 확인을 누르기 전까지는 실제 Layer 객체를 건드리지 않는다.
    QVector<QColor> m_editedColors;
};

#endif // PRINTSTYLEDIALOG_H
