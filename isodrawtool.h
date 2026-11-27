#ifndef ISODRAWTOOL_H
#define ISODRAWTOOL_H

#include "abstracttool.h"
#include "projectionview.h"

#include <QDialog>
#include <QPointF>
#include <QSet>
#include <QString>

class QCheckBox;
class QRadioButton;

// ─────────────────────────────────────────────────────────────────────────────
// IsoDrawTool — 메뉴 "3D 기계용 > 등각도 그리기" (명령 iso3d), 부품 3D 보기 창 [도면에 그리기]
//   ① 설정창: 방향 [KS 등각도 / 3D 창 현재 화면] · [숨은선]
//   ② (메뉴에서 시작한 경우만) 도면에서 그릴 부품 고르기: 부품 클릭(다시 클릭 = 빼기), 빈 곳 클릭 → 왼→오 = 창, 오→왼 = 교차
//      Enter / Space / 오른쪽 클릭 = 끝 (하나도 안 고르면 전체 부품). 실행 전에 선택해 둔 부품이 있으면 그 부품으로 바로
//      3D 창 [도면에 그리기] 에서 시작하면 고르기 없이 창에 보이는 부품 그대로
//   ③ Blender 로 불리언 적용한 최종 모양 → IsoDrawing 으로 선 계산 (몇 초)
//   ④ 미리보기가 따라다님 → 클릭한 곳(가운데)에 놓기. 실선 = 현재 레이어, 숨은선 = "숨은선" 레이어, undo 한 번
//   한 번 그리고 끝 (모양을 고친 뒤에는 다시 그리기)
// ─────────────────────────────────────────────────────────────────────────────
class IsoDrawDialog : public QDialog {
    Q_OBJECT
public:
    IsoDrawDialog(bool windowOpen, bool preferView, QWidget *parent = nullptr);
    bool viewMode() const;       // 3D 창 현재 화면 방향
    bool hidden() const;         // 숨은선

private:
    QRadioButton *m_iso = nullptr;
    QRadioButton *m_view = nullptr;
    QCheckBox *m_hidden = nullptr;
};

class IsoDrawTool : public AbstractTool {
    Q_OBJECT
public:
    //   preferView = 3D 창 버튼에서 시작 (설정창 기본값 = 현재 화면)
    explicit IsoDrawTool(QObject *parent = nullptr, bool preferView = false);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

    // 선 계산 (메뉴·MCP 공용). viewMode = 3D 창 화면 방향, visibleOnly = 3D 창에 보이는 부품만
    //   onlyIds 가 비어 있지 않으면 그 부품만 (도면에서 고른 부품 UUID — visibleOnly 보다 우선)
    static bool compute(IAppContext *ctx, MyGraphicsScene *scene, bool viewMode, bool visibleOnly, bool hidden,
                        ProjDrawing &out, QString *error, const QSet<QString> &onlyIds = QSet<QString>());
    // 도면에 놓기 (at = 그림 가운데). 반환 = 넣은 선 수
    static int place(IAppContext *ctx, MyGraphicsScene *scene, const ProjDrawing &d, const QPointF &at);

private slots:
    void finishNow();

private:
    void cancelTool();
    // ── 부품 고르기 단계 ──
    void showPickPrompt();
    void setPartHighlight(const QString &id, bool on);   // 고른 부품 선을 선택 표시로 강조
    void finishBox(const QPointF &p2);                   // 창(왼→오)/교차(오→왼) → 부품 추가
    void endBox();                                       // 상자 미리보기 지우기
    void runCompute();                                   // 선 계산 → 놓기 단계

private slots:
    void finishPick();                                   // 고르기 끝 (Enter·Space·오른쪽 클릭)

private:
    bool m_preferView = false;
    bool m_viewMode = false;      // 설정창: 3D 창 현재 화면 방향
    bool m_hidden = false;        // 설정창: 숨은선
    bool m_fromWindow = false;    // 3D 창 버튼에서 시작 → 고르기 없이 창에 보이는 부품
    bool m_picking = false;       // 부품 고르는 중
    QSet<QString> m_picked;       // 고른 부품 UUID (비면 전체)
    bool m_boxSelecting = false;  // 빈 곳 클릭 → 두 번째 클릭까지 상자 선택
    QPointF m_firstCorner;
    bool m_ready = false;         // 선 계산 끝 → 놓을 곳 기다림
    ProjDrawing m_drawing;
};

#endif // ISODRAWTOOL_H
