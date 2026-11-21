#ifndef MECH3DVIEWTOOL_H
#define MECH3DVIEWTOOL_H

#include "abstracttool.h"

#include <QSet>
#include <QString>

// ─────────────────────────────────────────────────────────────────────────────
// 부품 3D 보기 — 메뉴 "3D 기계용 > 부품 3D 보기"
//   ① 도면에서 볼 부품을 클릭 (여러 개, 같은 부품을 다시 클릭하면 빼기)
//      빈 곳 클릭 → 두 번째 클릭: 왼→오 = 창(완전히 들어간 것), 오→왼 = 교차(걸친 것) 로 여러 부품 한꺼번에
//   ② Enter / Space / 오른쪽 클릭 = 끝 → 3D 보기 창. 하나도 안 고르고 끝내면 전체 부품.
//   Esc = 취소. 실행 전에 도면에서 부품을 선택해 두었으면 바로 그 부품으로 창을 연다.
//   고른 부품은 창 왼쪽 부품 목록에 체크되어 열리며, 창에서 다시 켜고 끌 수 있다.
// ─────────────────────────────────────────────────────────────────────────────
class Mech3DViewTool : public AbstractTool {
    Q_OBJECT
public:
    explicit Mech3DViewTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void deactivate() override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

    // 3D 보기 창을 열어 ids 부품만 보이게 한다 (빈 집합 = 전체)
    static void openWindow(IAppContext *ctx, const QSet<QString> &ids);

private:
    void showPrompt();
    void finishPick();
    void cancelPick();
    void setPartHighlight(const QString &id, bool on);
    void finishBox(const QPointF &p2);   // 창(L→R)/교차(R→L) 선택 → 부품 추가
    void endBox();                       // 상자 미리보기 지우기

    QSet<QString> m_picked;
    bool m_boxSelecting = false;         // 빈 곳 클릭 → 두 번째 클릭까지 상자 선택
    QPointF m_firstCorner;
    bool m_done = false;   // Enter 키와 명령줄 빈 입력이 함께 와도 한 번만 끝낸다
};

#endif // MECH3DVIEWTOOL_H
