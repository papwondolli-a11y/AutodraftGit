#ifndef INPUTPARSER_H
#define INPUTPARSER_H

#include <QString>
#include <QPointF>

class InputParser
{
public:
    InputParser();
    ~InputParser();

    // [핵심 함수]
    // input: 입력 문자열 ("20,30", "@10<45", "500")
    // basePoint: 기준점 (상대좌표/거리입력 시 필요)
    // mousePos: 마우스 위치 (거리입력 시 방향 결정용)
    // isOrtho: 직교 모드 (F8) 여부
    // resultPoint: 계산된 결과 (참조로 반환)
    bool parseCoordinate(const QString& input,
                         const QPointF& basePoint,
                         const QPointF& mousePos,
                         bool isOrtho,
                         QPointF& resultPoint);

    // [Phase 3] 입력 단위 → 모델(mm) 환산 배율.  mm 도면=1.0, inch 도면=25.4.
    //   AppContext::parser() 가 매 호출마다 활성 도면 단위로 세팅한다.
    //   좌표/거리 입력값에만 곱하며, 각도 입력에는 적용하지 않는다.
    void setUnitScale(double s) { m_unitScale = (s > 0.0) ? s : 1.0; }
    double unitScale() const { return m_unitScale; }

private:
    double m_unitScale = 1.0;
};

#endif // INPUTPARSER_H
