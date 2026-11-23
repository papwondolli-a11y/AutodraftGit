#ifndef EXTRUDETOOL_H
#define EXTRUDETOOL_H

#include "abstracttool.h"

#include <QDialog>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>

class QCheckBox;
class QDoubleSpinBox;
class QLineEdit;

// ─────────────────────────────────────────────────────────────────────────────
// ExtrudeTool — 메뉴 "3D 기계용 > 돌출" (명령 ext3d)
//   ① 해치 명령처럼 공간 고르기: 사각 두 점 → 내부 공간 클릭(여러 곳, 엔터) 또는 [복합모양(B)]
//      단면 회전체의 좌·우측면도에 그린 공간 (투상 원도 경계)
//   ② 설정창: 이름 · 시작 거리(그 측면 쪽 부품 끝면에서) · 깊이 · [호스트에 합치기]
//   → ExtrudeFeature 등록 → 3D·다른 투상도 갱신
// ─────────────────────────────────────────────────────────────────────────────
//   hostName 이 비면 "단독 돌출" 설정창: 이름 · 시작 Z · 깊이(두께) · [가운데 대칭]
class QLabel;

class ExtrudeDialog : public QDialog {
    Q_OBJECT
public:
    //   plateBoss = 판의 측면도·보조 투상도 안 공간 → 그 면에 붙는 돌기 (시작 거리 없음)
    ExtrudeDialog(const QString &hostName, const QString &viewName, QWidget *parent = nullptr, bool plateBoss = false);
    QString partName() const;
    double offset() const;
    double depth() const;
    bool merge() const;
    bool symmetric() const;   // 단독 돌출: 시작 Z 를 가운데로
    // 단독 돌출: 시작 Z 를 미리 채움 (공간 아래 판의 윗면 — 판 위에 올리기), note = 설명 한 줄
    void setStartZ(double z, const QString &note);

private:
    bool m_standalone = false;
    bool m_plateBoss = false;
    QLineEdit *m_name = nullptr;
    QDoubleSpinBox *m_offset = nullptr;
    QDoubleSpinBox *m_depth = nullptr;
    QCheckBox *m_merge = nullptr;
    QCheckBox *m_symmetric = nullptr;
    QLabel *m_startNote = nullptr;   // 시작 Z 자동 설명 (단독 돌출)
};

// 파내기 설정창: 이름 · (회전체 홈이면 시작 거리) · 깊이 · [관통](판만)
class CutDialog : public QDialog {
    Q_OBJECT
public:
    enum Mode { PlateFace = 0, PlateFront = 1, RevolveSide = 2 };
    CutDialog(Mode mode, const QString &hostName, const QString &viewName, QWidget *parent = nullptr);
    QString partName() const;
    double offset() const;
    double depth() const;
    bool through() const;

private:
    QLineEdit *m_name = nullptr;
    QDoubleSpinBox *m_offset = nullptr;
    QDoubleSpinBox *m_depth = nullptr;
    QCheckBox *m_through = nullptr;
};

class ExtrudeTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ExtrudeTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;
    bool processCommand(const QString &command) override;

protected:
    bool m_cut = false;      // 파내기 도구 (CutTool)

private:
    enum State { AreaBox1, AreaBox2, AreaPick };

    void showPrompt();
    void finishArea(bool complexShape);
    void finishCut();        // 파내기: 판 측면·보조 뷰 / 판 정면 / 회전체 측면도 → 등록
    void cancelTool();

    State m_state = AreaBox1;
    QPointF m_box1;
    QRectF m_box;
    QList<QPointF> m_picks;
    QPainterPath m_region;
};

// CutTool — 메뉴 "3D 기계용 > 파내기" (명령 cut3d): 돌출과 같은 공간 고르기, 재료를 뺀다
//   판의 측면도·보조 투상도 안 = 그 면에서 안쪽으로, 판 정면(판 공간 안 닫힌 모양) = 앞면에서 −Z, 회전체 측면도 = 끝면에서 홈
class CutTool : public ExtrudeTool {
    Q_OBJECT
public:
    explicit CutTool(QObject *parent = nullptr);
};

#endif // EXTRUDETOOL_H
