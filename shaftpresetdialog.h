#ifndef SHAFTPRESETDIALOG_H
#define SHAFTPRESETDIALOG_H

#include "shaftdata.h"

#include <QDialog>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

// ─────────────────────────────────────────────────────────────────────────
// ShaftPresetDialog — 축 설정창의 "데이터 저장 / 데이터 불러오기"
//   저장 위치: <data>/shaft/<이름>.shaft (ShaftParams JSON, 옛 형식도 읽힘)
//   <data> 는 exe 옆 data → 상위 폴더 data → 소스 폴더 data 순으로 찾는다 (기하공차 프리셋과 같은 규칙).
//   불러오기 창: 왼쪽 이름 목록 + 오른쪽 미리보기 그림·요약, [이름 바꾸기] [삭제] [불러오기] [취소].
// ─────────────────────────────────────────────────────────────────────────
class ShaftPresetDialog : public QDialog {
    Q_OBJECT
public:
    explicit ShaftPresetDialog(QWidget *parent = nullptr);

    // 고른 축 데이터 (exec() 가 Accepted 일 때만 유효)
    ShaftParams params() const { return m_params; }
    bool hasPresets() const;

    // 이름을 물어 params 를 저장한다. 저장했으면 true.
    static bool savePreset(QWidget *parent, const ShaftParams &params);
    // 프리셋 폴더 (create=true 면 없을 때 만든다)
    static QString presetDir(bool create);

private slots:
    void onCurrentChanged(QListWidgetItem *current, QListWidgetItem *previous);
    void onItemDoubleClicked(QListWidgetItem *item);
    void onRename();
    void onDelete();
    void onLoad();

private:
    void refreshList(const QString &select = QString());
    QString currentFile() const;
    static QString sanitizeName(const QString &name);
    static QString summaryText(const ShaftParams &p);

    QListWidget *m_list    = nullptr;
    QLabel      *m_preview = nullptr;
    QLabel      *m_summary = nullptr;
    QPushButton *m_rename  = nullptr;
    QPushButton *m_delete  = nullptr;
    QPushButton *m_load    = nullptr;
    ShaftParams  m_params;
};

#endif // SHAFTPRESETDIALOG_H
