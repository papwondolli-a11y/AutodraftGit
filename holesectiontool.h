#ifndef HOLESECTIONTOOL_H
#define HOLESECTIONTOOL_H

#include "abstracttool.h"
#include "myhole.h"
#include "holesectiongenerator.h"

#include <QPen>

class HoleItem;

// 선택한 구멍(HoleItem)의 스펙으로 단면(측면) 프로파일 선들을 1회성 생성하는 도구.
// 평면도의 HoleItem과 실시간 연동되지 않음 — 생성 후에는 일반 선 객체로 취급.
class HoleSectionTool : public AbstractTool {
    Q_OBJECT
public:
    enum State { WaitSelectHole, WaitPlacePoint, WaitPlaceMidPoint, WaitSetDirection };

    explicit HoleSectionTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

    // 헤드리스(MCP): 구멍 정보 다이얼로그 없이 구멍을 직접 지정한다.
    // value = 관통이면 재료 두께, 막힌 구멍이면 탭 깊이(0 이하면 다이얼로그 기본값과 동일:
    // 관통=구멍 지름, 막힘=구멍의 기존 탭 깊이). hiddenLayer 가 비어있지 않으면 숨은선 모드.
    // 성공하면 WaitPlacePoint 상태가 되어 이어서 기준점/방향점을 mousePress 로 받는다.
    bool selectHoleHeadless(HoleItem *hole, double value, const QString &hiddenLayer);

private:
    void cancelTool();
    void showSelectPrompt();
    void showPlacePrompt();
    void showMidPointPrompt();
    void showDirectionPrompt();
    void placeSection(const QPointF &direction);
    // 구멍 요소(기본/탭/카운터보어/카운터싱크/중심선)별로 HoleTool과 동일한 규칙의 레이어명을 반환
    QString layerNameForRole(HoleSectionLineRole role) const;
    // 지정 레이어의 색상/두께뿐 아니라 선종류(점선/일점쇄선 등)까지 대시 패턴으로 반영한 펜을 반환.
    // AbstractTool::getLayerPen과 달리 레이어명이 (DXF 임포트 등으로) 대소문자만 달라도 매칭하고,
    // "Continuous"/"Continous" 및 홀수 길이 패턴을 명시적으로 걸러 항상 실선이 되던 문제를 방지한다.
    QPen sectionPenForLayer(const QString &layerName) const;
    // 2점 지정 모드일 때 기준점-방향점 사이 거리를 재료두께(관통)/탭깊이(막힘)에 덮어씀
    void applyTwoPointOverride(const QPointF &direction);

    State m_state;
    MyHole m_selectedHole;
    QString m_selectedHoleGroupId; // 선택된 구멍의 HOLE_DATA_GROUP_ID (단면 태깅용, 없으면 새로 발급)
    double m_materialThickness;
    bool m_twoPointMode; // true면 수동 입력값 대신 기준점-방향점 거리를 사용
    // 구멍 정보 다이얼로그의 "숨은선" 체크박스 — 켜져 있으면 생성되는 단면 선 전부(기본/탭/
    // 카운터보어/카운터싱크/중심선 구분 없이)를 m_hiddenLineLayer 레이어로 그린다.
    bool m_hiddenLineMode = false;
    QString m_hiddenLineLayer;
    // 마지막으로 고른 숨은선 레이어를 세션/재실행 간에 기억한다(QSettings 영속).
    // 다이얼로그 콤보가 매번 첫 레이어(대개 "0", 실선)로 리셋되어, 사용자가 숨은선
    // 레이어를 매번 다시 지정하지 않으면 실선으로 그려지던 문제를 막기 위함.
    static QString s_hiddenLineLayer;
    QPointF m_currentMousePos;
    QPointF m_topCenter; // WaitSetDirection 단계에서 기준이 되는 상단면 기준점
    QPointF m_midPointFirst; // WaitPlaceMidPoint 단계에서 저장해 두는 첫 번째 점(p1, shift+클릭)
};

#endif // HOLESECTIONTOOL_H
