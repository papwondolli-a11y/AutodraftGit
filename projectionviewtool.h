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
    enum State { PickPart, PickKind, Place };

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
};

class ProjectionRegenTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ProjectionRegenTool(QObject *parent = nullptr);
    void activate(MyGraphicsScene *scene) override;

private slots:
    void finishNow();
};

#endif // PROJECTIONVIEWTOOL_H
