#ifndef DOORLAYERSETTINGS_H
#define DOORLAYERSETTINGS_H

#include <QString>

class MyGraphicsScene;

// ─────────────────────────────────────────────────────────────────────────────
// DoorLayerSettings — 평면도어 탭 전 도어 공용 레이어 설정
//
//   문틀        : 잼브·문짝 폴리라인 (단순문은 문짝선)
//   나머지      : 연결선·스윙 호   (단순문은 호)
//   Trim 제외   : 벽 트림 시 건드리지 않을 레이어 (빈 문자열 = 없음)
//
// 값은 도면(ToolDefaults 네임스페이스 "PlanDoorLayers")에 저장된다.
// 아직 값이 없으면 기존 PlanDoorTool 네임스페이스의 값을 씨앗으로 사용한다.
// ─────────────────────────────────────────────────────────────────────────────
class DoorLayerSettings {
public:
    static QString frameLayer();
    static void    setFrameLayer(const QString &name);
    static QString otherLayer();
    static void    setOtherLayer(const QString &name);
    static QString trimExcludeLayer();
    static void    setTrimExcludeLayer(const QString &name);

    // 문틀 상세치수 (mm) — 초기값 J=45, S=30, K=40
    static double jambOuter();                 // J: 문틀 외곽 폭
    static void   setJambOuter(double v);
    static double stopWidth();                 // S: 스톱 폭 (경첩 X 위치, 호 반지름 R=H-2S)
    static void   setStopWidth(double v);
    static double stepHeight();                // K/T: 단 높이
    static void   setStepHeight(double v);

    static void load(MyGraphicsScene *scene);
    static void save(MyGraphicsScene *scene);

private:
    static QString s_frameLayer;
    static QString s_otherLayer;
    static QString s_trimExcludeLayer;
    static double  s_jambOuter;
    static double  s_stopWidth;
    static double  s_stepHeight;
};

#endif // DOORLAYERSETTINGS_H
