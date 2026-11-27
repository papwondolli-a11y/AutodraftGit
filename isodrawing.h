#ifndef ISODRAWING_H
#define ISODRAWING_H

#include "buildingmodel3d.h"
#include "projectionview.h"

#include <QList>
#include <QString>
#include <QVector>
#include <QVector3D>

// ─────────────────────────────────────────────────────────────────────────────
// IsoDrawing — 3D 부품을 도면 선(등각도·3D 창 화면 방향)으로 (메뉴 "3D 기계용 > 등각도 그리기", 명령 iso3d)
//
//   ① 모양 확정: 3D 창 메시는 구멍·섬·파내기를 빼지 않으므로 Blender 를 창 없이(-b) 돌려
//      불리언을 적용한 최종 메시를 받는다 (나사커터는 꺼서 원통으로). Keyway 표시판은 빼고 보냄.
//   ② 선 뽑기: 꺾인 모서리(이웃 면 각도 ≥ 20°)·열린 변 + 굽은 면의 윤곽선(이웃 면이 앞/뒤로 갈림).
//      원을 이루는 잘게 나뉜 면 사이 모서리는 그리지 않음.
//   ③ 가림: 투상면 격자에 삼각형을 나눠 넣고, 선을 잘게 나눠 그 점 앞(보는 쪽)에 다른 면이 있으면 숨은선.
//   좌표: 모델 (x 오른쪽, y 위, z 보는 쪽 = 정면도 앞) mm.
//     KS 등각도 = 시선 (1, 1, 1)/√3, 위 = +y, 축 방향 실제 길이(×√1.5).  3D 창 화면 = 창의 요·피치, 위 = +z, 배율 1.
// ─────────────────────────────────────────────────────────────────────────────
struct IsoMesh {
    QString name;
    QVector<QVector3D> v;   // mm
    QVector<int> f;         // 삼각형 꼭짓점 번호 3개씩
};

class IsoDrawing {
public:
    // Blender 로 최종 메시 (불리언 적용). 실패하면 false + error
    static bool blenderMeshes(const Model3D &model, QList<IsoMesh> &out, QString *error);
    // 선 계산. view = 보는 사람 쪽 방향, up = 도면 위쪽이 될 방향, scale = 배율. 결과 선은 가운데가 (0, 0)
    static ProjDrawing build(const QList<IsoMesh> &meshes, const QVector3D &view, const QVector3D &up, double scale,
                             bool showHidden, double featureDeg = 20.0);
    // KS 등각도 방향 — 3D 창의 [등각] 버튼과 같은 방향 (요 kIsoYawDeg, 피치 kIsoPitchDeg)
    //   standUp 꺼짐 = 도면 Z 가 위 (도면 XY 가 바닥), 켜짐 = 도면 Y 가 위 (3D 창의 세우기와 같음)
    static void isoAxes(bool standUp, QVector3D &view, QVector3D &up, double &scale);
    // 등각 방향 (도): 오른쪽 앞에서 45°, 위에서 atan(1/√2) ≈ 35.26° → 세 축이 화면에서 120°
    static constexpr double kIsoYawDeg = -45.0;
    static constexpr double kIsoPitchDeg = 35.26438968;
    // 3D 창 요·피치(도) → 방향 (창과 같은 식)
    //   standUp = 창이 부품을 세워 보여 주는 모델 (Model3D::standUp) → 모델 좌표 방향으로 되돌림
    static void orbitAxes(double yawDeg, double pitchDeg, bool standUp, QVector3D &view, QVector3D &up, double &scale);
};

#endif // ISODRAWING_H
