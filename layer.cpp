#include "layer.h"

Layer::Layer(const QString &name, const QColor &color, double weight,
             const QString &linetype, bool current,
             bool layerOn, bool plotOn, bool layerLock)
    : m_current(current),
    m_name(name),
    m_color(color),
    m_weight(weight),
    m_linetype(linetype),
    m_layerOn(layerOn),
    m_plotOn(plotOn),
    m_layerLock(layerLock),
    m_printColor()
{}

bool Layer::isCurrent() const    { return m_current; }
QString Layer::name() const      { return m_name; }
QColor Layer::color() const      { return m_color; }
double Layer::weight() const     { return m_weight; }
QString Layer::linetype() const  { return m_linetype; }
bool Layer::isLayerOn() const    { return m_layerOn; }
bool Layer::isPlotOn() const     { return m_plotOn; }
bool Layer::isLayerLock() const  { return m_layerLock; }
QColor Layer::printColor() const { return m_printColor; }
bool Layer::hasPrintColorOverride() const { return m_printColor.isValid(); }

void Layer::setCurrent(bool v)            { m_current = v; }
void Layer::setName(const QString &v)     { m_name = v; }
void Layer::setColor(const QColor &v)     { m_color = v; }
void Layer::setWeight(double v)           { m_weight = v; }
void Layer::setLinetype(const QString &v) { m_linetype = v; }
void Layer::setLayerOn(bool v)            { m_layerOn = v; }
void Layer::setPlotOn(bool v)             { m_plotOn = v; }
void Layer::setLayerLock(bool v)          { m_layerLock = v; }
void Layer::setPrintColor(const QColor &color)  { m_printColor = color; }
void Layer::clearPrintColorOverride()           { m_printColor = QColor(); }
