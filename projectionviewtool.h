#ifndef PROJECTIONVIEWTOOL_H
#define PROJECTIONVIEWTOOL_H

#include "abstracttool.h"
#include "projectionview.h"

#include <QList>
#include <QPointF>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// ProjectionViewTool — 메뉴 "3D 기계용 > 투상도 만들기" (명령 proj3d)
//   ① 3D 데이터가 있는 부품 클릭(회전체 해칭·축·베어링 외형)
//   ② 방향: 좌측면(L) / 우측면(R) / 평면(T, 외형)  (엔터 = 좌측면), 숨은선(H) 켜기/끄기 (기본 끔)
//      판(단독 돌출)은 면 선택(F): 판의 변을 클릭 → 그 면을 정면으로 보는 보조 투상도 (법선 방향으로 기울여 정렬)
//      돌출(받침대)이 붙은 회전체도 면 선택(F): 측면도에서 돌출의 변을 클릭 → 그 면 방향 보조 투상도 (평면도처럼 축과 나란히)
//   ③ 놓을 곳 클릭 — 정면과 투상 정렬(측면 = 축선 위, 평면 = 축과 나란히), 미리보기 따라다님
// ProjectionRegenTool — "투상도 다시 그리기" (명령 proj3dupd): 모든 투상도를 지우고 현재 모양으로 다시 그림
// ─────────────────────────────────────────────────────────────────────────────
class ProjectionViewTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ProjectionViewTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

private:
    enum State { PickPart, PickKind, PickFace, Place };

    void showPrompt();
    void setKind(int kind);
    void cancelTool();

    State m_state = PickPart;
    QString m_part;
    QList<RevolveLoop> m_loops;
    QPointF m_a0;
    double m_angle = 0.0;
    QString m_name;
    int m_kind = ProjectionView::Left;
    bool m_hidden = false;   // 숨은선 그리기 (기본 끔)
    bool m_plate = false;    // 고른 부품이 판(단독 돌출) — 면 선택(F) 가능
    bool m_revFace = false;  // 회전체에 받침대 등 돌출이 붙어 있음 — 면 선택(F): 측면도의 돌출 변 → 회전체 보조 투상도
};

class ProjectionRegenTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ProjectionRegenTool(QObject *parent = nullptr);
    void activate(MyGraphicsScene *scene) override;

private slots:
    void finishNow();
};

// ProjectionHideTool — "투상도 숨기기" (명령 proj3dhide): 투상도 선·원을 클릭하면 그 뷰를 숨김 (여러 번, Esc 끝)
//   기록은 남아 측면도의 구멍이 부품에 계속 연결된다 (정면 구멍 단면·3D 구멍 유지). 그 구멍도 화면에서 숨김.
class ProjectionHideTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ProjectionHideTool(QObject *parent = nullptr);
    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;

private:
    void showPrompt();
    void cancelTool();
    // 클릭 위치의 투상도 뷰 (선 위·근처). 못 찾으면 false
    bool pickView(const QPointF &pos, QString &partUuid, QString &viewId);
};

// ProjectionShowTool — "투상도 보이기" (명령 proj3dshow): 숨긴 투상도 목록에서 골라 다시 그림 (하나뿐이면 바로)
class ProjectionShowTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ProjectionShowTool(QObject *parent = nullptr);
    void activate(MyGraphicsScene *scene) override;

private slots:
    void runNow();
};

#endif // PROJECTIONVIEWTOOL_H
