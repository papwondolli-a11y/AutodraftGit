#include "openingheights.h"
#include "layermanager.h"
#include "plandoorsimpletool.h"
#include "scenesymbolregistry.h"

const char *const OpeningHeights::kSillKey   = "OpenSill";
const char *const OpeningHeights::kHeightKey = "OpenH";

bool   OpeningHeights::s_hasSillOverride   = false;
double OpeningHeights::s_sillOverride      = 0.0;
bool   OpeningHeights::s_hasHeightOverride = false;
double OpeningHeights::s_heightOverride    = 0.0;

// 공장 기본값 (레이어 높이 미설정 시)
static const double kDoorSill = 0.0;
static const double kDoorH    = 2100.0;
static const double kWinSill  = 900.0;
static const double kWinH     = 1200.0;

// 레이어 높이가 설정(> 0)된 경우에만 그 레이어를 돌려준다
static Layer *configuredLayer(LayerManager *layers, const QString &layerName)
{
    if (!layers || layerName.isEmpty())
        return nullptr;
    Layer *layer = layers->getLayer(layerName);
    if (!layer || layer->height() <= 0.0)
        return nullptr;
    return layer;
}

bool OpeningHeights::isDoorType(const QString &t)
{
    return t == QLatin1String("PlanDoor") || t == QLatin1String("PlanDoorDouble") ||
           t == QLatin1String("PlanDoorBiSwing") || t == QLatin1String("PlanDoorDoubleBiSwing") ||
           t == QLatin1String("PlanRevolvingDoor") || PlanDoorSimpleTool::isSimpleSymbolType(t);
}

bool OpeningHeights::isWindowType(const QString &t)
{
    return t == QLatin1String("PlanWindow");
}

bool OpeningHeights::isOpeningType(const QString &t)
{
    return isDoorType(t) || isWindowType(t);
}

double OpeningHeights::defaultSill(LayerManager *layers, const QString &layerName, bool window)
{
    if (Layer *layer = configuredLayer(layers, layerName))
        return layer->elevation();
    return window ? kWinSill : kDoorSill;
}

double OpeningHeights::defaultHeight(LayerManager *layers, const QString &layerName, bool window)
{
    if (Layer *layer = configuredLayer(layers, layerName))
        return layer->height();
    return window ? kWinH : kDoorH;
}

void OpeningHeights::stamp(QMap<QString, QVariant> &props, const QString &symbolType,
                           LayerManager *layers)
{
    if (!isOpeningType(symbolType))
        return;
    const bool window = isWindowType(symbolType);
    const QString layerName = props.value(QStringLiteral("FrameLayer")).toString();
    const QString sillKey   = QString::fromLatin1(kSillKey);
    const QString heightKey = QString::fromLatin1(kHeightKey);
    if (!props.contains(sillKey))
        props[sillKey] = s_hasSillOverride ? s_sillOverride
                                           : defaultSill(layers, layerName, window);
    if (!props.contains(heightKey))
        props[heightKey] = s_hasHeightOverride ? s_heightOverride
                                               : defaultHeight(layers, layerName, window);
}

double OpeningHeights::sillOf(const SymbolInfo &info, LayerManager *layers)
{
    const QString key = QString::fromLatin1(kSillKey);
    if (info.props.contains(key))
        return info.props.value(key).toDouble();
    return defaultSill(layers, info.props.value(QStringLiteral("FrameLayer")).toString(),
                       isWindowType(info.type));
}

double OpeningHeights::heightOf(const SymbolInfo &info, LayerManager *layers)
{
    const QString key = QString::fromLatin1(kHeightKey);
    if (info.props.contains(key))
        return info.props.value(key).toDouble();
    return defaultHeight(layers, info.props.value(QStringLiteral("FrameLayer")).toString(),
                         isWindowType(info.type));
}

void OpeningHeights::setOverride(bool hasSill, double sill, bool hasHeight, double height)
{
    s_hasSillOverride   = hasSill;
    s_sillOverride      = sill;
    s_hasHeightOverride = hasHeight;
    s_heightOverride    = height;
}

void OpeningHeights::clearOverride()
{
    setOverride(false, 0.0, false, 0.0);
}
