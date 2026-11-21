#ifndef OPENINGHEIGHTS_H
#define OPENINGHEIGHTS_H

#include <QMap>
#include <QString>
#include <QVariant>

class LayerManager;
struct SymbolInfo;

// ─────────────────────────────────────────────────────────────────────────────
// OpeningHeights — 문·창 심볼의 3D 개구부 높이 (로드맵 1단계)
//
//   심볼 props 키:
//     "OpenSill" : 개구부 하단 높이 (mm, 문 = 0, 창 = 창대 높이)
//     "OpenH"    : 개구부 높이 (mm, 하단에서 위로)
//
//   기본값 = 심볼 문틀 레이어(props "FrameLayer")의 3D Layer 값
//     레이어 기준높이 → 하단높이, 레이어 높이 → 개구부 높이.
//     레이어 높이가 0(미설정)이면 공장 기본값: 문 0/2100, 창 900/1200.
//   값은 배치 시 심볼마다 기록되므로, 같은 레이어 안에서 예외인 문·창은
//   속성창에서 개별로 고친다. 키가 없는 예전 심볼은 sillOf()/heightOf() 가
//   레이어 기본값으로 대신한다. 평면 형상과 무관해 재생성은 필요 없다.
// ─────────────────────────────────────────────────────────────────────────────
class OpeningHeights {
public:
    static const char *const kSillKey;   // "OpenSill"
    static const char *const kHeightKey; // "OpenH"

    static bool isDoorType(const QString &symbolType);
    static bool isWindowType(const QString &symbolType);
    static bool isOpeningType(const QString &symbolType);

    // 레이어 기준 기본값 (layers 가 null 이거나 레이어 높이가 0 이면 공장 기본값)
    static double defaultSill(LayerManager *layers, const QString &layerName, bool window);
    static double defaultHeight(LayerManager *layers, const QString &layerName, bool window);

    // 배치 시 호출: 아직 값이 없으면 (1회성 지정값 > 문틀 레이어 값) 순으로 기록.
    // props 에 "FrameLayer" 가 들어간 뒤에 호출해야 한다.
    static void stamp(QMap<QString, QVariant> &props, const QString &symbolType,
                      LayerManager *layers);

    // 읽기 (값이 없으면 레이어 기본값)
    static double sillOf(const SymbolInfo &info, LayerManager *layers);
    static double heightOf(const SymbolInfo &info, LayerManager *layers);

    // MCP 등에서 다음 배치에만 적용할 값. clearOverride() 로 해제.
    static void setOverride(bool hasSill, double sill, bool hasHeight, double height);
    static void clearOverride();

private:
    static bool   s_hasSillOverride;
    static double s_sillOverride;
    static bool   s_hasHeightOverride;
    static double s_heightOverride;
};

#endif // OPENINGHEIGHTS_H
