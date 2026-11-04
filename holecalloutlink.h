#ifndef HOLECALLOUTLINK_H
#define HOLECALLOUTLINK_H

#include <QList>
#include <QString>

class HoleItem;
class IAppContext;
class LeaderItem;
class QGraphicsItem;
class QWidget;

// 구멍 콜아웃(LeaderItem) ↔ 구멍(HoleItem) 양방향 연동.
//
// - 콜아웃은 MyLeader::holeIds에 구멍의 HOLE_DATA_GROUP_ID를 저장해 연결을 기억한다.
// - 연결된 콜아웃의 텍스트는 항상 구멍 데이터에서 다시 조립된다(refreshCallout).
//   HoleItem이 스펙이 바뀔 때마다(속성창·그립·undo/redo 모두) refreshCalloutsForHole을
//   부르므로, 텍스트 자체는 undo 스택에 넣지 않아도 구멍 상태와 항상 일치한다.
// - 콜아웃 텍스트를 직접 편집하면(applyTextEdit) 텍스트를 구멍 스펙으로 해석해
//   구멍 속성 변경 명령을 undo 스택에 넣는다 — 텍스트는 위 경로로 자동 갱신된다.
class HoleCalloutLink {
public:
    // 구멍의 고유 ID(HOLE_DATA_GROUP_ID). 없으면 새 UUID를 발급해 붙인다.
    static QString ensureHoleId(QGraphicsItem *hole);

    // 씬에서 콜아웃에 연결된 구멍들을 찾는다(블록 내부 구멍은 제외).
    static QList<HoleItem *> linkedHoles(const LeaderItem *leader);

    // 구멍 데이터 변경 직후 호출 — 이 구멍을 가리키는 모든 콜아웃 텍스트를 다시 조립.
    static void refreshCalloutsForHole(HoleItem *hole);

    // 콜아웃 하나의 텍스트를 연결된 구멍 데이터로 다시 조립. 구멍 스펙이 서로 다르면
    // (예: 여러 구멍 중 하나만 속성창에서 수정) 한 줄로 표현할 수 없으므로 텍스트를 그대로 둔다.
    static void refreshCallout(LeaderItem *leader);

    // 콜아웃 텍스트 편집 적용. 연결된 구멍이 없는 일반 지시선이면 false를 반환하며,
    // 이때 호출부는 기존대로 Content 변경을 직접 처리해야 한다. true면 이 함수가 처리를 끝냈다.
    static bool applyTextEdit(IAppContext *ctx, LeaderItem *leader, const QString &newText,
                              QWidget *parent);

private:
    static void applyTextOnly(IAppContext *ctx, LeaderItem *leader, const QString &newText);
};

#endif // HOLECALLOUTLINK_H
