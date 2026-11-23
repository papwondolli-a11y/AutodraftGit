#ifndef MODEL3DWINDOW_H
#define MODEL3DWINDOW_H

#include "buildingmodel3d.h"

#include <QPointer>
#include <QSet>
#include <QStringList>
#include <QWidget>

class IAppContext;
class DrawSubWindow;
class Model3DView;
class QComboBox;
class QLabel;
class QListWidget;
class QListWidgetItem;

// ─────────────────────────────────────────────────────────────────────────────
// Model3DWindow — 메뉴 "3D 건축용 > 3D 보기" / "3D 기계용 > 부품 3D 보기" 창 (로드맵 3단계)
//   대상 도면에서 BuildingModel3D 로 모델을 뽑아 Model3DView 에 보여준다.
//   도면을 고친 뒤 [새로고침] 으로 다시 뽑는다. 창은 하나만 띄워 재사용한다.
// ─────────────────────────────────────────────────────────────────────────────
class Model3DWindow : public QWidget {
    Q_OBJECT
public:
    explicit Model3DWindow(IAppContext *context, QWidget *parent = nullptr);
    ~Model3DWindow() override;

    // 열려 있으면 그 창, 없으면 새로 만든다 (메뉴·MCP 공용, 창은 하나)
    static Model3DWindow *instance(IAppContext *context, QWidget *parent);
    // 이미 열린 창 (없으면 nullptr, 새로 만들지 않음)
    static Model3DWindow *existing();
    // 지금 보이는 모델 (기계용 = 고른 부품만) — 등각도 그리기
    const Model3D &currentModel() const { return m_model; }

    // 건축용 = BuildingModel3D(레이어 높이로 벽 돌출), 기계용 = MechModel3D(기계 Tools 파라메트릭 부품)
    enum Mode { Architecture = 0, Mechanical };
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    // 대상 도면을 바꾸고 다시 추출한다 (resetCamera = 카메라를 등각으로)
    void showDrawing(DrawSubWindow *drawing, bool resetCamera);
    Model3DView *view() const { return m_view; }

    // ── 기계용 부품 고르기 ──
    // ids 부품만 보이기 (빈 집합이면 전체 보이기)
    void showOnlyParts(const QSet<QString> &ids);
    // 이름(축-1 …)으로 고르기 — MCP. 모르는 이름은 unknown 에 담는다
    void showOnlyPartNames(const QStringList &names, QStringList *unknown = nullptr);
    // 지금 보이는 부품 이름
    QStringList visiblePartNames() const;

private slots:
    void onPartItemChanged(QListWidgetItem *item);
    void onPartsAll();
    void onPartsNone();
    void onRefresh();
    void onIso();
    void onTop();
    void onFront();
    void onSide();
    void onExportObj();     // OBJ 파일로 저장 (위치 선택)
    void onOpenBlender();   // 임시 OBJ + Blender 실행
    void onDrawIso();       // 기계용: 지금 화면 방향으로 도면에 등각도 그리기 (IsoDrawTool)
    void onPrecisionChanged(int index);   // 기계용: 원 둘레 분할 수

private:
    QString drawingBaseName() const;
    void rebuildPartList();     // m_fullModel 부품으로 목록 다시 채우기 (체크 = m_hidden 반영)
    void applyPartFilter(bool resetCamera);   // m_hidden 을 빼서 m_model·화면·상태줄 갱신

    static QPointer<Model3DWindow> s_instance;
    Model3D m_model;        // 지금 보이는 모델 (내보내기·Blender 용, 기계용은 고른 부품만)
    Model3D m_fullModel;    // 기계용: 도면 전체 부품
    QSet<QString> m_hidden; // 기계용: 끈 부품 UUID (새로고침해도 유지, 새 부품은 켜짐)
    QWidget *m_partPanel = nullptr;
    QListWidget *m_partList = nullptr;
    Mode m_mode = Architecture;
    IAppContext *m_appContext = nullptr;
    QPointer<DrawSubWindow> m_drawing;
    Model3DView *m_view = nullptr;
    QLabel *m_status = nullptr;
    QLabel *m_precisionLabel = nullptr;
    QComboBox *m_precision = nullptr;   // 기계용에서만 보임
    QWidget *m_drawIso = nullptr;       // 기계용에서만 보임 [도면에 그리기]
};

#endif // MODEL3DWINDOW_H
