#ifndef BOOLTOOL_H
#define BOOLTOOL_H

#include "abstracttool.h"

#include <QDialog>
#include <QList>
#include <QPainterPath>
#include <QString>

class QGraphicsItem;
class QLabel;
class QLineEdit;
class QRadioButton;

// ─────────────────────────────────────────────────────────────────────────────
// 부품 합치기 (메뉴 "3D 기계용 > 부품 합치기", 명령 bool3d)
//   ① 남길 판 A 클릭 → ② 도구 판 B 클릭 → 설정창(합치기 A∪B / 빼기 A−B / 겹침 A∩B, 이름)
//   도면에 A 주황 · B 파란 점선 · 결과 회색 채움 미리보기. 스케치 선은 그대로 (선을 고치면 결과도 따라감)
//   계산은 ExtrudeFeature::setBool / resolve (Z 범위가 같으면 평면 모양 하나, 다르면 Blender 불리언)
// 부품 합치기 풀기 (bool3dclear): 합친 판(A 또는 B)을 클릭하면 원래 두 판으로
// ─────────────────────────────────────────────────────────────────────────────
class BoolDialog : public QDialog {
    Q_OBJECT
public:
    BoolDialog(const QString &aText, const QString &bText, const QString &aName, QWidget *parent = nullptr);
    int op() const;                // ExtrudeFeature::BoolOp
    QString partName() const;
    void setNote(const QString &text);

signals:
    void previewChanged();

private slots:
    void onToggled(bool on);

private:
    QRadioButton *m_union = nullptr;
    QRadioButton *m_subtract = nullptr;
    QRadioButton *m_intersect = nullptr;
    QLineEdit *m_name = nullptr;
    QLabel *m_note = nullptr;
};

class BoolTool : public AbstractTool {
    Q_OBJECT
public:
    explicit BoolTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;

private slots:
    void updatePreview();          // 설정창의 방법이 바뀜 → 결과 미리보기 다시

private:
    void showPrompt();
    void finish();
    void cancelTool();
    void clearPreview();
    void drawPicked();             // 고른 A·B 강조

    QString m_a;
    QString m_b;
    BoolDialog *m_dialog = nullptr;
    QList<QGraphicsItem *> m_preview;
};

// 선택 모드에서 오른쪽 클릭: 합친 판이면 [스케치선 켜기 / 스케치선 끄기 / 합치기 풀기] 메뉴. 처리했으면 true
class BoolMenu {
public:
    static bool exec(IAppContext *ctx, MyGraphicsScene *scene, const QPointF &pos);
    // 합치기 풀기 (undo 매크로 포함) — 풀린 판 수
    static int unbool(IAppContext *ctx, MyGraphicsScene *scene, const QString &uuid);
    // 스케치선 끄기(hide) / 보기 — 끄면 결과 외형선만 그려 두고 스케치 선은 숨김 (undo 매크로 포함)
    static void setSketch(IAppContext *ctx, MyGraphicsScene *scene, const QString &a, bool hide);
    // 스케치선을 끈 합친 부품 A 를 통째로 지움: 숨긴 스케치 선 + A·B 기록(투상도·붙은 돌기·파내기 포함). undo 매크로 안에서
    //   결과 외형선을 모두 지웠을 때 (ProjectionView::syncAfterErase) — 기록이 남으면 저장 모양으로 3D 에 계속 나옴
    static void deletePart(IAppContext *ctx, MyGraphicsScene *scene, const QString &a);

private:
    // 결과 외형선 (뷰 태그 "boolout:<A>") 그리기 / 지우기 — undo 매크로 안에서
    static void drawOutline(IAppContext *ctx, MyGraphicsScene *scene, const QString &a);
    static void removeOutline(IAppContext *ctx, MyGraphicsScene *scene, const QString &a);
};

class UnboolTool : public AbstractTool {
    Q_OBJECT
public:
    explicit UnboolTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;
};

#endif // BOOLTOOL_H
