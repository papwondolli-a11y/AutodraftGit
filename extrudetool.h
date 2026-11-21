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
class ExtrudeDialog : public QDialog {
    Q_OBJECT
public:
    ExtrudeDialog(const QString &hostName, const QString &viewName, QWidget *parent = nullptr);
    QString partName() const;
    double offset() const;
    double depth() const;
    bool merge() const;

private:
    QLineEdit *m_name = nullptr;
    QDoubleSpinBox *m_offset = nullptr;
    QDoubleSpinBox *m_depth = nullptr;
    QCheckBox *m_merge = nullptr;
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

private:
    enum State { AreaBox1, AreaBox2, AreaPick };

    void showPrompt();
    void finishArea(bool complexShape);
    void cancelTool();

    State m_state = AreaBox1;
    QPointF m_box1;
    QRectF m_box;
    QList<QPointF> m_picks;
    QPainterPath m_region;
};

#endif // EXTRUDETOOL_H
