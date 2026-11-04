#ifndef NEWFROMTEMPLATEDIALOG_H
#define NEWFROMTEMPLATEDIALOG_H

#include <QDialog>
#include <QString>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class IAppContext;

// ─────────────────────────────────────────────────────────────────────────────
// NewFromTemplateDialog  ("템플릿 보기")
//   - 대시보드(초기화면)에서만 열 수 있다.
//   - 목록 맨 위에 항상 "빈 도면", 그 아래로 data 폴더의 *.cdt.
//   - "찾아보기..."로 폴더 밖 템플릿도 1회성으로 지정 가능.
//   버튼:
//     만들기 - 목록 선택과 무관하게 새로운 빈 원형도면을 만든다(새 창 열림).
//     교체   - 선택한 항목을 '새로만들기(Ctrl+N)'의 기본 원형도면으로
//              설정만 한다. 새 창은 열지 않고 대시보드로 돌아간다.
//     편집   - 선택 템플릿 파일 자체를 열어 편집(설정값 유지, 저장 시 되쓰기)
//     삭제   - 선택 템플릿 파일 삭제(다이얼로그는 열린 채 목록만 갱신)
//     취소   - 닫기
// ─────────────────────────────────────────────────────────────────────────────
class NewFromTemplateDialog : public QDialog {
    Q_OBJECT
public:
    enum class Action { None, CreateNew, SetDefault, Edit };

    // ctx 는 "삭제" 시 대시보드 "원형도면" 콤보를 즉시 갱신하는 데만 쓰인다
    // (nullptr 이면 그냥 건너뜀).
    explicit NewFromTemplateDialog(IAppContext *ctx, QWidget *parent = nullptr);
    ~NewFromTemplateDialog() override;

    // exec() 가 Accepted 를 돌려줄 때 어떤 버튼이었는지.
    Action selectedAction() const { return m_action; }

    // 선택된 템플릿의 절대 경로. 빈 문자열이면 "빈 도면".
    // (CreateNew 액션에서는 의미 없음 — 선택과 무관하게 항상 빈 도면으로 시작한다.)
    QString selectedTemplatePath() const;

private slots:
    void onBrowse();
    void onNewClicked();
    void onReplaceClicked();
    void onEditClicked();
    void onDeleteClicked();
    void onItemDoubleClicked(QListWidgetItem *item);
    void onSelectionChanged();

private:
    void reloadList();
    bool currentIsRealFile() const; // "빈 도면"이 아니고 실제 파일이 선택됨

    IAppContext *m_ctx;
    QListWidget *m_list;
    QPushButton *m_browseButton;
    QPushButton *m_newButton;
    QPushButton *m_replaceButton;
    QPushButton *m_editButton;
    QPushButton *m_deleteButton;
    QPushButton *m_cancelButton;

    Action  m_action;
    QString m_browsedPath; // "찾아보기..."로 고른 폴더 밖 경로 (1회성)
};

#endif // NEWFROMTEMPLATEDIALOG_H
