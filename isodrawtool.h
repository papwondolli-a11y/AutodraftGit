#ifndef ISODRAWTOOL_H
#define ISODRAWTOOL_H

#include "abstracttool.h"
#include "projectionview.h"

#include <QDialog>
#include <QPointF>

class QCheckBox;
class QRadioButton;

// ─────────────────────────────────────────────────────────────────────────────
// IsoDrawTool — 메뉴 "3D 기계용 > 등각도 그리기" (명령 iso3d), 부품 3D 보기 창 [도면에 그리기]
//   ① 설정창: 방향 [KS 등각도 / 3D 창 현재 화면] · 부품 [3D 창에 보이는 부품만 / 전체] · [숨은선]
//   ② Blender 로 불리언 적용한 최종 모양 → IsoDrawing 으로 선 계산 (몇 초)
//   ③ 미리보기가 따라다님 → 클릭한 곳(가운데)에 놓기. 실선 = 현재 레이어, 숨은선 = "숨은선" 레이어, undo 한 번
//   한 번 그리고 끝 (모양을 고친 뒤에는 다시 그리기)
// ─────────────────────────────────────────────────────────────────────────────
class IsoDrawDialog : public QDialog {
    Q_OBJECT
public:
    IsoDrawDialog(bool windowOpen, bool preferView, QWidget *parent = nullptr);
    bool viewMode() const;       // 3D 창 현재 화면 방향
    bool visibleOnly() const;    // 3D 창에 보이는 부품만
    bool hidden() const;         // 숨은선

private:
    QRadioButton *m_iso = nullptr;
    QRadioButton *m_view = nullptr;
    QCheckBox *m_visibleOnly = nullptr;
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

    // 선 계산 (메뉴·MCP 공용). viewMode = 3D 창 화면 방향, visibleOnly = 3D 창에 보이는 부품만
    static bool compute(IAppContext *ctx, MyGraphicsScene *scene, bool viewMode, bool visibleOnly, bool hidden,
                        ProjDrawing &out, QString *error);
    // 도면에 놓기 (at = 그림 가운데). 반환 = 넣은 선 수
    static int place(IAppContext *ctx, MyGraphicsScene *scene, const ProjDrawing &d, const QPointF &at);

private slots:
    void finishNow();

private:
    void cancelTool();

    bool m_preferView = false;
    bool m_ready = false;
    ProjDrawing m_drawing;
};

#endif // ISODRAWTOOL_H
