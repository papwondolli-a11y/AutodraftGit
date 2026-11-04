#ifndef COLUMNTOOL_H
#define COLUMNTOOL_H

#include "abstracttool.h"
#include "commandbase.h"
#include "mydimension.h"
#include <QDialog>
#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QVector>

class QCheckBox;
class QComboBox;
class LayerManager;
class QDoubleSpinBox;
class QLabel;

// ─────────────────────────────────────────────────────────────────────────────
// ColumnParams — 기둥 설정 (colbox.lsp 의 프롬프트 입력값을 대화상자로 대체)
// ─────────────────────────────────────────────────────────────────────────────
struct ColumnParams {
    double width       = 600.0;   // 기둥 폭 DX
    double height      = 800.0;   // 기둥 깊이 DY
    bool   centered    = true;    // true : 기준점 = 기둥 중앙 (dx=DX/2, dy=DY/2)
    double centerDx    = 300.0;   // 기둥 좌하단 모서리 → 기준(중심선) 교점 거리 dx
    double centerDy    = 400.0;   // 기둥 좌하단 모서리 → 기준(중심선) 교점 거리 dy
    bool   eraseInside = true;    // 기둥 안쪽에 걸린 기존 선 지우기 (lsp: break + erase)
    bool   drawHatch   = true;    // 기둥 표시 사선 3개 (lsp: boxh)
    bool   drawCross   = true;    // 기준점 십자 표시 (column.cdb 의 빨간 선)
    bool   drawDims    = false;   // 치수선 (DX, DY, dx, dy)
    QString layer;                // 기둥 외곽/사선/치수 레이어 (빈 값 = 현재 레이어)
    QString markLayer = QStringLiteral("사용자입력");   // 기준점 십자 레이어

    double effDx() const { return centered ? width  / 2.0 : centerDx; }
    double effDy() const { return centered ? height / 2.0 : centerDy; }
};

// ─────────────────────────────────────────────────────────────────────────────
// ColumnGeom — 생성될 도형 묶음
// ─────────────────────────────────────────────────────────────────────────────
struct ColumnGeom {
    QRectF               rect;         // 기둥 외곽 (y 위쪽이 + 인 CAD 좌표)
    QVector<QLineF>      hatchLines;   // 사선 3개
    QVector<QLineF>      crossLines;   // 기준점 십자
    QVector<MyDimension> dims;         // dx, dy, DX, DY
};

// ─────────────────────────────────────────────────────────────────────────────
// ColumnTool — 기둥 (colbox.lsp)
//   설정창에서 폭/깊이/기준점 거리를 입력한 뒤, 중심선 기준점을 반복 지정해서 배치한다.
//   우클릭 / Esc : 종료
// ─────────────────────────────────────────────────────────────────────────────
class ColumnTool : public AbstractTool {
    Q_OBJECT

public:
    explicit ColumnTool(const ColumnParams &params, QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;
    void keyPress(QKeyEvent *event) override;

    static ColumnParams loadParams(MyGraphicsScene *scene);
    static void         saveParams(MyGraphicsScene *scene, const ColumnParams &p);

    // 기준점(중심선 교점)과 설정값으로 전체 도형 계산
    static ColumnGeom buildGeometry(const QPointF &base, const ColumnParams &p,
                                    double dimSpacing);


private:
    void place(const QPointF &base);
    void eraseInsideLines(const QRectF &rect);
    void ensureLayer(IAppContext *ctx, const QString &name, const QColor &color);
    void addItem(QGraphicsItem *item, const QString &layerName);
    double dimSpacing() const;
    QPainterPath previewPath(const ColumnGeom &g) const;

    ColumnParams m_params;
};

// ─────────────────────────────────────────────────────────────────────────────
// ColumnDialog — 기둥 설정 대화상자
// ─────────────────────────────────────────────────────────────────────────────
class ColumnDialog : public QDialog {
    Q_OBJECT

public:
    ColumnDialog(const ColumnParams &params, LayerManager *layerMgr,
                 QWidget *parent = nullptr);
    ColumnParams params() const;

private slots:
    void onSizeChanged();
    void onCenteredToggled(bool on);

private:
    static void populateLayerCombo(QComboBox *combo, LayerManager *mgr,
                                   const QString &selected, bool withCurrent);
    QDoubleSpinBox *m_spinW;
    QDoubleSpinBox *m_spinH;
    QCheckBox      *m_checkCentered;
    QDoubleSpinBox *m_spinDx;
    QDoubleSpinBox *m_spinDy;
    QCheckBox      *m_checkErase;
    QCheckBox      *m_checkHatch;
    QCheckBox      *m_checkCross;
    QCheckBox      *m_checkDims;
    QComboBox      *m_comboLayer;
    QComboBox      *m_comboMarkLayer;
    QLabel         *m_labelInfo;
};

// ─────────────────────────────────────────────────────────────────────────────
// ColumnCommand — 설정창 → 툴 시작
// ─────────────────────────────────────────────────────────────────────────────
class ColumnCommand : public CommandBase {
    Q_OBJECT

public:
    explicit ColumnCommand(IAppContext *ctx, QObject *parent = nullptr);
    void execute() override;

    static CommandBase *createCommand(IAppContext *c);

private:
    IAppContext *m_ctx;
};

#endif // COLUMNTOOL_H
