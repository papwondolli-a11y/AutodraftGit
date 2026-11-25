#ifndef EXTRUDETOOL_H
#define EXTRUDETOOL_H

#include "abstracttool.h"
#include "extrudetarget.h"

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
//   ② 설정창: 대상 · 방향 · 이름 · 시작 · 깊이 · [합치기] (ExtrudeDialog — 도면에 대상 강조·방향 화살표 미리보기)
//   → ExtrudeFeature 등록 → 3D·다른 투상도 갱신
// ─────────────────────────────────────────────────────────────────────────────
class QComboBox;
class QGraphicsItem;
class QLabel;

// 돌출·파내기 설정창: 대상(어느 부품의 어느 뷰) · 방향(대상에 따라 정해짐, 읽기 전용) · 이름 · 시작 · 깊이 · 합치기/대칭/관통
//   대상의 첫 항목 = 자동 판단값. 대상이나 값이 바뀌면 previewChanged → 도구가 도면 미리보기를 다시 그린다.
class ExtrudeDialog : public QDialog {
    Q_OBJECT
public:
    ExtrudeDialog(MyGraphicsScene *scene, const QList<ExtrudeTarget> &targets, bool cut, QWidget *parent = nullptr);
    ExtrudeTarget target() const;
    ExtrudeValues values() const;   // 시작·깊이·대칭·관통
    QString partName() const;
    bool merge() const;

signals:
    void previewChanged();

private slots:
    void onTargetChanged(int index);
    void onValueChanged();

private:
    QString defaultName(const ExtrudeTarget &t) const;

    MyGraphicsScene *m_scene = nullptr;
    QList<ExtrudeTarget> m_targets;
    bool m_cut = false;
    int m_lastType = -1;             // 바로 앞 대상 종류 (종류가 바뀔 때만 시작값을 기본값으로)
    QString m_autoName;              // 자동으로 넣은 이름 (사용자가 고쳤으면 그대로 둠)
    QComboBox *m_target = nullptr;
    QLabel *m_direction = nullptr;
    QLabel *m_warning = nullptr;     // 공간이 대상 측면도와 떨어져 있음
    QLineEdit *m_name = nullptr;
    QLabel *m_offsetLabel = nullptr; // 시작 거리 ↔ 시작 Z
    QDoubleSpinBox *m_offset = nullptr;
    QLabel *m_startNote = nullptr;   // 시작 Z 자동 설명
    QDoubleSpinBox *m_depth = nullptr;
    QCheckBox *m_merge = nullptr;
    QCheckBox *m_symmetric = nullptr;
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

private slots:
    void updatePreview();    // 설정창의 대상·값이 바뀜 → 도면 미리보기 다시

private:
    enum State { AreaBox1, AreaBox2, AreaPick };

    void showPrompt();
    void finishArea(bool complexShape);
    // 같은 공간을 다른 대상으로 돌출한 옛 기록을 어떻게 할지 물어봄. false = 취소. remove = 지울 기록
    bool askOldRecords(const ExtrudeTarget &t, QWidget *parent, QStringList &remove);
    void clearPreview();
    void cancelTool();

    State m_state = AreaBox1;
    QPointF m_box1;
    QRectF m_box;
    QList<QPointF> m_picks;
    QPainterPath m_region;
    ExtrudeDialog *m_dialog = nullptr;        // 설정창이 열려 있는 동안만
    QList<QGraphicsItem *> m_preview;         // 임시 미리보기 도형
};

// CutTool — 메뉴 "3D 기계용 > 파내기" (명령 cut3d): 돌출과 같은 공간 고르기, 재료를 뺀다
//   판의 측면도·보조 투상도 안 = 그 면에서 안쪽으로, 판 정면(판 공간 안 닫힌 모양) = 앞면에서 −Z, 회전체 측면도 = 끝면에서 홈
class CutTool : public ExtrudeTool {
    Q_OBJECT
public:
    explicit CutTool(QObject *parent = nullptr);
};

#endif // EXTRUDETOOL_H
