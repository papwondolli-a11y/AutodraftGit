#ifndef MECHMODEL3D_H
#define MECHMODEL3D_H

#include "buildingmodel3d.h"
#include "extrudefeature.h"
#include "holefeature.h"
#include "revolveprofile.h"

#include <QList>
#include <QPointF>
#include <QSet>
#include <QString>

class MyGraphicsScene;
class QGraphicsItem;
struct ShaftParams;

// ─────────────────────────────────────────────────────────────────────────────
// MechModel3D — 기계 부품 파라메트릭 3D (메뉴 "3D 기계용")
//
//   기계 Tools 가 배치할 때 저장한 파라미터(SceneSymbolRegistry)로 정확한 3D 메시를 만든다.
//   도면에서 부품을 찾는 기준 = MECH_DATA_PART_ID 가 붙은 닫힌 외형 폴리라인.
//     · 외형을 옮기면(MOVE) refX/refY 와의 차이만큼 3D 도 옮긴다. (회전·대칭은 아직 반영 안 함)
//     · 외형을 지우면 그 부품은 3D 에서 빠진다.
//
//   좌표: 도면 X·Y(mm) = 측면도 평면, Z = 보는 사람 쪽(+). 축선은 도면의 수평선(Y = oy, Z = 0).
//   지원: MechShaft(축) — 구간 원기둥 + 모따기 원뿔대(나사 모따기 포함),
//         나사부 = 어두운 띠(Thread 재질), 키홈 = 표면 표시(Keyway) + Blender 불리언용 절삭체(Cutter).
// ─────────────────────────────────────────────────────────────────────────────
class MechModel3D {
public:
    static Model3D build(MyGraphicsScene *scene);

    // 축 하나 (origin = 왼쪽 끝면 중심) → 본체 + 키홈 절삭체
    static QList<Model3DMeshPart> shaftParts(const ShaftParams &params, const QPointF &origin,
                                             const QString &name);

    // 깊은 홈 볼베어링 (origin = 베어링 중심, angleDeg = 축선 방향) → 내·외륜 + 볼
    static QList<Model3DMeshPart> deepGrooveBearingParts(double d, double D, double B, double r,
                                                         const QPointF &origin, double angleDeg,
                                                         const QString &name);

    // 육각볼트 (origin = 머리/몸통 경계 중심, angleDeg = 몸통 방향) → 육각 머리 + 몸통(나사 띠)
    static QList<Model3DMeshPart> hexBoltParts(double dNom, double s, double k, double L, double pitch,
                                               double threadLen, const QPointF &origin, double angleDeg,
                                               const QString &name);

    // 해칭 단면 회전체 (origin = 회전축 시작점, angleDeg = 축 방향, loops = RevolveProfile 축 좌표 단면)
    //   fits = 원통면 끼워맞춤 기호 (키 RevolveCylinder::key(), 예 "H:37" → "H7")
    //   holes = 축 방향 구멍(원주 배열) → "<이름>_구멍커터"(Blender 불리언) + 3D 보기용 입구 표시
    static QList<Model3DMeshPart> revolveParts(const QList<RevolveLoop> &loops, const QPointF &origin,
                                               double angleDeg, const QString &name,
                                               const QMap<QString, QString> &fits,
                                               const QList<HoleFeature> &holes = QList<HoleFeature>());

    // 투상도용: 부품(uuid)의 회전 단면(축 좌표 x·r, 바깥 고리 반시계 / 구멍 고리 시계)과 현재 축 시작점·방향.
    //   회전체가 아닌 부품(볼트 등)이거나 찾지 못하면 false
    static bool revolveSection(MyGraphicsScene *scene, const QString &uuid, QList<RevolveLoop> &loops,
                               QPointF &origin, double &angleDeg, QString &name, QPointF *refOut = nullptr);

    // 측면도 돌출 (ExtrudeFeature) → 닫힌 프리즘. partName = 부품 이름(합치기면 "<호스트>_합침N")
    static Model3DMeshPart extrudePart(const ExtrudeInfo &info, const QString &partName);

    static QString summaryText(const Model3D &model);

    // ── 부품 고르기 (부품 3D 보기 목록 · MCP parts) ──
    struct PartEntry {
        QString id;     // 레지스트리 UUID
        QString name;   // 본체 메시 이름 (축-1, 볼트 M12x50-1 …)
    };
    // 모델에 든 부품 목록 (도면 순서)
    static QList<PartEntry> partList(const Model3D &model);
    // hiddenIds 부품(그 커터·합침 포함)을 뺀 모델
    static Model3D filtered(const Model3D &model, const QSet<QString> &hiddenIds);
    // 도면에서 선택한 도형들이 속한 부품 UUID (외형·해칭·돌출선·투상도 선, 태그 없는 선은 위치로)
    static QSet<QString> partIdsOfItems(MyGraphicsScene *scene, const QList<QGraphicsItem *> &items);

    // 원 둘레 분할 수 (정밀도). 원기둥 면이 실제 원보다 r·(1-cos(π/n)) 만큼 안쪽에 생긴다
    //   64 → Ø20 에서 0.012 mm, 128 → 0.003 mm, 256 → 0.0008 mm.  QSettings "mech3d/radialSteps"
    static int radialSteps();
    static void setRadialSteps(int steps);   // 32~512 로 제한, 저장
    static double facetError(double radius) { return facetError(radius, radialSteps()); }
    static double facetError(double radius, int steps);
};

#endif // MECHMODEL3D_H
