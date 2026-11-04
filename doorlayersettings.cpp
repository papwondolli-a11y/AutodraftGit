#include "doorlayersettings.h"
#include "tooldefaults.h"
#include <QtGlobal>

QString DoorLayerSettings::s_frameLayer;
QString DoorLayerSettings::s_otherLayer;
QString DoorLayerSettings::s_trimExcludeLayer;
double  DoorLayerSettings::s_jambOuter  = 45.0;
double  DoorLayerSettings::s_stopWidth  = 30.0;
double  DoorLayerSettings::s_stepHeight = 40.0;

double DoorLayerSettings::jambOuter()            { return s_jambOuter; }
void   DoorLayerSettings::setJambOuter(double v)  { if (v > 0.0) s_jambOuter = v; }
double DoorLayerSettings::stopWidth()            { return s_stopWidth; }
void   DoorLayerSettings::setStopWidth(double v)  { if (v > 0.0) s_stopWidth = v; }
double DoorLayerSettings::stepHeight()           { return s_stepHeight; }
void   DoorLayerSettings::setStepHeight(double v) { s_stepHeight = qBound(10.0, v, 50.0); }

QString DoorLayerSettings::frameLayer()                      { return s_frameLayer; }
void    DoorLayerSettings::setFrameLayer(const QString &n)   { s_frameLayer = n; }
QString DoorLayerSettings::otherLayer()                      { return s_otherLayer; }
void    DoorLayerSettings::setOtherLayer(const QString &n)   { s_otherLayer = n; }
QString DoorLayerSettings::trimExcludeLayer()                { return s_trimExcludeLayer; }
void    DoorLayerSettings::setTrimExcludeLayer(const QString &n) { s_trimExcludeLayer = n; }

void DoorLayerSettings::load(MyGraphicsScene *scene)
{
    ToolDefaults d("PlanDoorLayers", scene);
    ToolDefaults legacy("PlanDoorTool", scene);   // 이전 버전 값 이월용
    s_frameLayer       = d.getString("FrameLayer",
                                     legacy.getString("FrameLayer", QString()));
    s_otherLayer       = d.getString("OtherLayer",
                                     legacy.getString("OtherLayer", QString()));
    s_trimExcludeLayer = d.getString("TrimExcludeLayer",
                                     legacy.getString("TrimExcludeLayer", QString()));
    setJambOuter(d.getDouble("JambOuter",   45.0));
    setStopWidth(d.getDouble("StopWidth",   30.0));
    setStepHeight(d.getDouble("StepHeight", 40.0));
}

void DoorLayerSettings::save(MyGraphicsScene *scene)
{
    ToolDefaults d("PlanDoorLayers", scene);
    d.put("FrameLayer",       s_frameLayer);
    d.put("OtherLayer",       s_otherLayer);
    d.put("TrimExcludeLayer", s_trimExcludeLayer);
    d.put("JambOuter",        s_jambOuter);
    d.put("StopWidth",        s_stopWidth);
    d.put("StepHeight",       s_stepHeight);
}
