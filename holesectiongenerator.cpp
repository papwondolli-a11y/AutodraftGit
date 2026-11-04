#include "holesectiongenerator.h"
#include <QtMath>
#include <algorithm>
#include <cmath>

QPointF HoleSectionGenerator::toWorld(const QPointF &topCenter, const QPointF &widthAxis,
                                      const QPointF &depthAxis, double u, double v) {
    return topCenter + widthAxis * u + depthAxis * v;
}

QList<HoleSectionLine> HoleSectionGenerator::buildLines(const MyHole &hole, const QPointF &topCenter,
                                                         double materialThickness,
                                                         const QPointF &direction) {
    QList<HoleSectionLine> lines;

    // 깊이 방향 단위벡터 (0벡터가 들어오면 기본값 +Y로 대체)
    double dlen = std::hypot(direction.x(), direction.y());
    QPointF depthAxis = (dlen > 1e-9) ? QPointF(direction.x() / dlen, direction.y() / dlen)
                                       : QPointF(0.0, 1.0);
    // 폭(가로) 방향은 깊이 방향에 수직 — 기본값(+Y)일 때 +X와 일치하도록 회전
    QPointF widthAxis(depthAxis.y(), -depthAxis.x());

    // 로컬좌표계: u = 폭 방향 오프셋, v = topCenter로부터의 깊이 방향 오프셋
    double curTopV = 0.0;
    double curR = hole.diameter / 2.0;

    // 탭 나사 호칭지름(major diameter) 반지름 — 골지름선에서 사용, 막힘 탭 구멍의 골->외형
    // 대각선 계산에도 필요해 여기서 미리 구함
    double majorDiaSpec = 0.0, pitchSpec = 0.0;
    double majorR = MyHole::parseThreadSpec(hole.threadSpec, majorDiaSpec, pitchSpec) ? majorDiaSpec / 2.0
                                                                                       : curR / 0.85;

    // 전체 파낸 깊이 (막힌 구멍: depth 필드, 관통: 재료두께 기준)
    double totalDepth = hole.throughHole ? materialThickness : hole.depth;
    if (totalDepth <= 0) totalDepth = hole.diameter; // 안전장치: 값 미입력 시 최소 표시

    // 1. 카운터보어 단(step)
    if (hole.hasCounterbore && hole.cbDiameter > curR * 2.0) {
        double cbR = hole.cbDiameter / 2.0;
        double cbBottomV = curTopV + std::min(hole.cbDepth, totalDepth);

        lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -cbR, curTopV),
                                        toWorld(topCenter, widthAxis, depthAxis, -cbR, cbBottomV)),
                                 HoleSectionLineRole::Counterbore};
        lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, cbR, curTopV),
                                        toWorld(topCenter, widthAxis, depthAxis, cbR, cbBottomV)),
                                 HoleSectionLineRole::Counterbore};
        // 단 바닥 (카운터보어 지름 -> 기본 지름으로 좁아지는 닫힘선, p1-p2와 동일하게 하나의 선으로 연결)
        lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -cbR, cbBottomV),
                                        toWorld(topCenter, widthAxis, depthAxis, cbR, cbBottomV)),
                                 HoleSectionLineRole::Counterbore};

        curTopV = cbBottomV;
    }

    // 2. 카운터싱크 챔퍼 (경사선)
    if (hole.hasCountersink && hole.csDiameter > curR * 2.0) {
        double csR = hole.csDiameter / 2.0;
        double halfAngleRad = qDegreesToRadians(hole.csAngle / 2.0);
        double chamferDepth = (halfAngleRad > 0.0) ? (csR - curR) / qTan(halfAngleRad) : 0.0;
        chamferDepth = std::min(chamferDepth, totalDepth);
        double chamferBottomV = curTopV + chamferDepth;

        lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -csR, curTopV),
                                        toWorld(topCenter, widthAxis, depthAxis, -curR, chamferBottomV)),
                                 HoleSectionLineRole::Countersink};
        lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, csR, curTopV),
                                        toWorld(topCenter, widthAxis, depthAxis, curR, chamferBottomV)),
                                 HoleSectionLineRole::Countersink};
        // 챔퍼 바닥 닫힘선 (p1-p2, 카운터보어 단과 동일한 표기 방식)
        lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -curR, chamferBottomV),
                                        toWorld(topCenter, widthAxis, depthAxis, curR, chamferBottomV)),
                                 HoleSectionLineRole::Countersink};

        curTopV = chamferBottomV;
    }

    double remainingDepth = std::max(0.0, totalDepth - curTopV);
    double bottomV = curTopV + remainingDepth;

    // 탭 막힘 구멍: 골지름선(나사 호칭지름, majorR — 내경 나사의 골은 호칭지름 쪽이 더 큰
    // 지름이 됨)은 탭깊이까지만 그림(p1-p2). 외형선(실제 드릴로 뚫린 표면, curR)은 탭깊이+2mm
    // 까지 그림(p3-p4) — 드릴팁 콘은 그 지점에서 시작됨. 대각선(p2->p7)은 p2에서 폭 방향
    // (p2-p6)과 60도(-30도 방향)를 이루며 외형선 연장선과 만나는 점(p7)까지 별도로 연결.
    bool blindThreaded = hole.isThreaded && !hole.throughHole;
    double rootBottomV = curTopV + hole.threadDepth;
    double runoutAngleRad = qDegreesToRadians(60.0); // p2-p6와 p2-p7 사이 각
    double runoutDepth = (majorR - curR) * qTan(runoutAngleRad); // p2 -> p7의 깊이방향 이동량
    double diagonalEndV = rootBottomV + runoutDepth; // p7의 v좌표 (대각선 끝점, 외형선 길이와는 무관)
    // 탭-드릴 여유: 호칭지름(majorDiaSpec) M12 이상은 +3mm, 미만은 +2mm
    double tapDrillClearance = (majorDiaSpec >= 12.0) ? 3.0 : 2.0;
    double outlineBottomV = rootBottomV + tapDrillClearance; // 외형선(p3-p4) 끝점 — 탭깊이 + 여유
    if (blindThreaded) bottomV = outlineBottomV;

    // 3. 기본 구멍 벽 (외형선, 드릴 지름)
    lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -curR, curTopV),
                                    toWorld(topCenter, widthAxis, depthAxis, -curR, bottomV)),
                             HoleSectionLineRole::Base};
    lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, curR, curTopV),
                                    toWorld(topCenter, widthAxis, depthAxis, curR, bottomV)),
                             HoleSectionLineRole::Base};

    // 4. 탭(나사) 호칭지름(major diameter) 선 — 골지름선, 평면도와 동일 기준
    if (hole.isThreaded) {
        if (blindThreaded) {
            // 골지름선(majorR): 탭깊이까지만 (p1-p2)
            lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -majorR, curTopV),
                                            toWorld(topCenter, widthAxis, depthAxis, -majorR, rootBottomV)),
                                     HoleSectionLineRole::Thread};
            lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, majorR, curTopV),
                                            toWorld(topCenter, widthAxis, depthAxis, majorR, rootBottomV)),
                                     HoleSectionLineRole::Thread};
            // 골지름선 하단 닫힘선 (p2-p6, 중심을 가로지름) — 외형선(Base) 레이어 사용
            lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -majorR, rootBottomV),
                                            toWorld(topCenter, widthAxis, depthAxis, majorR, rootBottomV)),
                                     HoleSectionLineRole::Base};
            // 골지름선 끝(p2/p6) -> 외형선 연장선과의 교점(p7) 대각선 (60도 각도로 계산된 지점)
            lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -majorR, rootBottomV),
                                            toWorld(topCenter, widthAxis, depthAxis, -curR, diagonalEndV)),
                                     HoleSectionLineRole::Thread};
            lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, majorR, rootBottomV),
                                            toWorld(topCenter, widthAxis, depthAxis, curR, diagonalEndV)),
                                     HoleSectionLineRole::Thread};
        } else {
            double threadBottomV = hole.throughHole ? bottomV
                                                     : std::min(curTopV + hole.threadDepth, bottomV);
            lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -majorR, curTopV),
                                            toWorld(topCenter, widthAxis, depthAxis, -majorR, threadBottomV)),
                                     HoleSectionLineRole::Thread};
            lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, majorR, curTopV),
                                            toWorld(topCenter, widthAxis, depthAxis, majorR, threadBottomV)),
                                     HoleSectionLineRole::Thread};

            // 막힌 구멍에서 나사산이 전체 깊이까지 도달하지 않는 경우: 나사 유효깊이 경계를
            // 표시하는 폭 방향 닫힘선 추가 (카운터보어 단과 동일한 표기 방식)
            if (!hole.throughHole && threadBottomV < bottomV - 1e-9) {
                lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -majorR, threadBottomV),
                                                toWorld(topCenter, widthAxis, depthAxis, majorR, threadBottomV)),
                                         HoleSectionLineRole::Thread};
            }
        }
    }

    // 5. 막힌 구멍 하단 - 드릴 팁 각도(약 118도, 축기준 반각 59도) 근사 표현
    if (!hole.throughHole) {
        // 원통부(드릴 지름)가 끝나고 콘이 시작되는 경계 닫힘선 (p1-p2)
        lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -curR, bottomV),
                                        toWorld(topCenter, widthAxis, depthAxis, curR, bottomV)),
                                 HoleSectionLineRole::Base};

        double coneHalfAngleRad = qDegreesToRadians(59.0);
        double coneDepth = curR / qTan(coneHalfAngleRad);
        double tipV = bottomV + coneDepth;
        lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, -curR, bottomV),
                                        toWorld(topCenter, widthAxis, depthAxis, 0.0, tipV)),
                                 HoleSectionLineRole::Base};
        lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, curR, bottomV),
                                        toWorld(topCenter, widthAxis, depthAxis, 0.0, tipV)),
                                 HoleSectionLineRole::Base};
        bottomV = tipV;
    }

    // 6. 중심선 (센터라인) - 상/하 여유 포함
    double margin = hole.diameter * 0.25;
    lines << HoleSectionLine{QLineF(toWorld(topCenter, widthAxis, depthAxis, 0.0, -margin),
                                    toWorld(topCenter, widthAxis, depthAxis, 0.0, bottomV + margin)),
                             HoleSectionLineRole::Center};

    return lines;
}
