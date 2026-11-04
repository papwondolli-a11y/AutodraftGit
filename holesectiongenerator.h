#ifndef HOLESECTIONGENERATOR_H
#define HOLESECTIONGENERATOR_H

#include "myhole.h"
#include <QLineF>
#include <QList>
#include <QPointF>

// 단면 선 하나가 구멍의 어떤 요소(기본/탭/카운터보어/카운터싱크/중심선)에 해당하는지 구분.
// 호출부(HoleSectionTool)가 HoleTool과 동일한 규칙으로 레이어를 적용할 수 있도록 함.
enum class HoleSectionLineRole { Base, Thread, Counterbore, Countersink, Center };

struct HoleSectionLine {
    QLineF line;
    HoleSectionLineRole role;
};

// 구멍(MyHole) 스펙으로부터 단면(측면) 형상 선분들을 생성하는 순수 함수 모음.
// 평면도의 HoleItem과 실시간 연동되지 않는 1회성 생성용 — 생성 이후 결과 선들은
// 일반 LineItem으로 취급되어 평면도 수정과 독립적으로 편집/삭제 가능.
class HoleSectionGenerator {
public:
    // topCenter: 재료 상단면에서 구멍 중심의 위치 (단면 프로파일의 기준점)
    // materialThickness: 관통 구멍일 때만 사용 (막힌 구멍은 depth로 하단이 결정됨)
    // direction: 파고드는(깊이) 방향 벡터 — 기본값 (0,1)은 씬 좌표 +Y(아래쪽). 정규화하여 사용.
    static QList<HoleSectionLine> buildLines(const MyHole &hole, const QPointF &topCenter,
                                             double materialThickness,
                                             const QPointF &direction = QPointF(0.0, 1.0));

private:
    // topCenter를 원점으로 하는 로컬좌표(u=폭 방향, v=깊이 방향)를 씬 좌표로 변환
    static QPointF toWorld(const QPointF &topCenter, const QPointF &widthAxis,
                           const QPointF &depthAxis, double u, double v);
};

#endif // HOLESECTIONGENERATOR_H
