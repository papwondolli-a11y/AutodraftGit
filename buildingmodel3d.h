#ifndef BUILDINGMODEL3D_H
#define BUILDINGMODEL3D_H

#include <QList>
#include <QPointF>
#include <QPolygonF>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QVector3D>

class MyGraphicsScene;
class LayerManager;

// ─────────────────────────────────────────────────────────────────────────────
// 건물 3D 모델 (로드맵 2단계 — 3D 보기·물량·단면/입면이 공유하는 공통 코어)
//
//   모든 좌표는 도면(CAD) 좌표 mm, Y 위쪽 +. 높이(z)도 mm.
//   Solid = 바닥 다각형(구멍 포함)을 z0~z1 로 수직 돌출한 덩어리.
// ─────────────────────────────────────────────────────────────────────────────
struct Model3DSolid {
    enum Kind { Wall = 0, OpeningFill = 1 }; // 벽 / 개구부 위·아래에 남는 벽(인방·창대 아래)
    Kind kind = Wall;
    QString layer;            // 높이를 가져온 레이어
    QList<QPolygonF> rings;   // [0] = 바깥 경계(CCW), 이후 = 구멍
    double z0 = 0.0;
    double z1 = 0.0;
    QString openingId;        // OpeningFill 인 경우 소속 개구부 심볼 UUID

    double planArea() const;  // 바닥 면적 (mm², 구멍 제외)
    double volume() const;    // 체적 (mm³)
};

struct Model3DOpening {
    QString id;               // 심볼 UUID
    QString type;             // PlanDoor, PlanWindow ...
    QString name;             // "문-1", "창문-1"
    bool isWindow = false;
    QPolygonF footprint;      // 벽 안의 개구부 사각형 (평면)
    double width = 0.0;       // 개구부 폭
    double depth = 0.0;       // 벽 두께 방향 길이
    double sill = 0.0;        // 하단 높이 (벽 바닥 기준)
    double height = 0.0;      // 개구부 높이
    bool hosted = false;      // 높이가 있는 벽 안에 놓였는가
    QString hostLayer;
    double hostZ0 = 0.0;
    double hostZ1 = 0.0;
};

// 임의 삼각형 메시 부품 (기계 부품 등 돌출로 표현할 수 없는 형상). 좌표 mm, Z 위.
struct Model3DMeshGroup {
    QString material;           // Steel | Thread | Keyway | Cutter ...
    QVector<QVector3D> tris;    // 점 3개씩, 바깥에서 볼 때 반시계
};

// 끼워맞춤 판정용 원통면 (기계 부품). 좌표는 도면 mm, 편차는 지름 기준 mm.
//   축선 = origin + dir·t, 원통면은 t ∈ [x0, x1], 반지름 radius. hole = 구멍(안쪽 면), 아니면 축(바깥 면).
struct Model3DFitFeature {
    double ox = 0.0, oy = 0.0, oz = 0.0;
    double dx = 1.0, dy = 0.0, dz = 0.0;
    double x0 = 0.0, x1 = 0.0;
    double radius = 0.0;
    bool hole = false;
    bool hasTol = false;        // 공차 지정됨 (false 면 호칭 치수 그대로)
    double upper = 0.0;         // 위 치수 허용차 (mm)
    double lower = 0.0;         // 아래 치수 허용차 (mm)
    QString label;              // "Ø20 k5", "안지름 Ø20 (베어링 0급)"
};

struct Model3DMeshPart {
    QString name;               // "축-1"
    QString type;               // "MechShaft"
    QList<Model3DMeshGroup> groups;
    QString cutterFor;          // 비어 있지 않으면 그 부품에서 빼낼 절삭체(키홈) — 3D 보기에는 그리지 않음
    QList<Model3DFitFeature> fits;  // 끼워맞춤 원통면 (OBJ 옆 <이름>_fits.json 으로 내보냄)
    QString partId;             // 도면 부품 UUID (SceneSymbolRegistry) — 부품 고르기용. 커터·합침도 주인 부품 id
};

struct Model3DWarning {
    QString message;
    QPointF at;               // 문제 위치 (도면 좌표)
};

struct Model3D {
    QList<Model3DSolid> solids;
    QList<Model3DOpening> openings;
    QList<Model3DWarning> warnings;
    QList<Model3DMeshPart> meshes; // 기계 부품 메시 (3D 기계용)
    // 세우기 (기계용, 도면마다 저장 — 3D 보기의 [세우기] 버튼, 기본 꺼짐). 좌표는 언제나 도면 X·Y·Z 그대로이고
    //   보는 방향만 바꾼다: 꺼짐 = 화면 위 Z, 켜짐 = 화면 위 Y. Blender 는 물체 회전 X 90° (로컬 축 = 도면 축)
    //   부품 종류로 추정하지 않고 전체를 함께 세운다 (MechModel3D::build 가 도면 설정에서 읽음)
    bool standUp = false;
    QStringList layers;       // 사용한 높이 레이어
    int segmentCount = 0;     // 입력 선분 수 (호는 분할 후)
    int faceCount = 0;        // 찾은 닫힌 영역 수
    int openEndCount = 0;     // 닫히지 않은 선 끝(빈틈) 수
    int autoCapCount = 0;     // 자동으로 닫은 벽 끝 수 (손으로 그린 도면의 문·창 자리)
};

// ─────────────────────────────────────────────────────────────────────────────
// BuildingModel3D — 2D 도면 + 레이어 높이 + 문·창 심볼 → Model3D
//
//   1) 높이(height > 0)가 있고 켜진 레이어의 선·폴리라인·호를 모은다
//      (다중선 center/space/mortar 선, 문·창 심볼 구성선, 블록 내부는 제외)
//   2) 문·창 심볼마다 벽 속 개구부 사각형을 가상 경계선으로 넣는다
//   3) 교차점에서 선을 나눠 평면 분할을 만들고 닫힌 영역(면)을 모두 찾는다
//   4) 도면 바깥(빈 공간)부터 실제 선을 하나 넘을 때마다 빈 공간 ↔ 벽을 번갈아 정한다
//   5) 벽 면은 경계 길이가 가장 긴 레이어의 높이로 돌출, 개구부 면은 하단·상단 벽만 남긴다
//   벽 끝이 닫히지 않은 곳 등은 warnings 에 위치와 함께 남는다.
// ─────────────────────────────────────────────────────────────────────────────
class BuildingModel3D {
public:
    static Model3D build(MyGraphicsScene *scene, LayerManager *layers);
    static QString summaryText(const Model3D &model);
    static double signedArea(const QPolygonF &ring); // CCW 양수
};

#endif // BUILDINGMODEL3D_H
