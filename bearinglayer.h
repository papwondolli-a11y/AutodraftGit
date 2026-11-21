#ifndef BEARINGLAYER_H
#define BEARINGLAYER_H

#include <QString>

class MyGraphicsScene;

// ─────────────────────────────────────────────────────────────────────────
// 베어링 3종(깊은홈볼/테이퍼롤러/스러스트) 공용 "베어링 레이어" 설정.
// 베어링 선택 대화상자의 레이어 콤보에서 고른 값을 ToolDefaults("BearingCommon")에 저장하고,
// 각 툴이 외형(와셔·볼·롤러 등) 도형을 그릴 때 읽어 쓴다. 기본값은 "BEARING".
// ─────────────────────────────────────────────────────────────────────────
namespace BearingLayer {
QString defaultName();
QString bodyLayer(MyGraphicsScene *scene);
void    setBodyLayer(MyGraphicsScene *scene, const QString &name);
}

#endif // BEARINGLAYER_H
