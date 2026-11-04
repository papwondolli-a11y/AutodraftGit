#ifndef LINETYPESELECTDIALOG_H
#define LINETYPESELECTDIALOG_H

#include <QDialog>

#include "linetypemanager.h"

class QListView;
class QStandardItemModel;
class QRadioButton;
class AppContext;

// 레이어 dock 의 Linetype 셀을 클릭했을 때 뜨는 선종류 선택 다이얼로그.
//  - 목록: 선종류 모양 미리보기(QPainter) + 이름
//  - OK 버튼 위: 그룹 라디오 3개 (AutoDraft / AutoCAD호환(S) / AutoCAD호환(ISO))
//  - 라디오는 LineTypeManager::currentGroup() 과 연동 → "새 레이어 만들기" 다이얼로그와 공유·유지
class LineTypeSelectDialog : public QDialog {
    Q_OBJECT
public:
    LineTypeSelectDialog(AppContext *context, const QString &currentLinetype,
                         QWidget *parent = nullptr);

    // 선택된 선종류 이름. 유효한 선택이 없으면 빈 문자열.
    QString selectedLinetype() const;

private slots:
    void onGroupChanged();

private:
    LineTypeManager::LinetypeGroup currentRadioGroup() const;
    void reloadList();

    AppContext *m_appContext;
    QString m_currentLinetype; // 편집 대상 레이어의 기존 선종류

    QListView *m_listView;
    QStandardItemModel *m_model;
    QRadioButton *m_rbAutoDraft;
    QRadioButton *m_rbAcadStd;
    QRadioButton *m_rbAcadIso;
};

#endif // LINETYPESELECTDIALOG_H
