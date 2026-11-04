#include "stylemanager.h"

StyleManager::StyleManager(QObject *parent)
    : QObject(parent)
{
    initData();
}

StyleManager::~StyleManager()
{
}

void StyleManager::initData()
{

    // 여기서 초기화하면 tr() 번역이 완벽하게 작동합니다.
   m_colors = {
        {tr("Black"), Qt::black},
        {tr("White"), Qt::white},
        {tr("Red"), Qt::red},
        {tr("Dark red"), Qt::darkRed},
        {tr("Green"), Qt::green},
        {tr("Dark Green"), Qt::darkGreen},
        {tr("Blue"), Qt::blue},
        {tr("Dark Blue"), Qt::darkBlue},
        {tr("Cyan"), Qt::cyan},
        {tr("Dark Cyan"), Qt::darkCyan},
        {tr("Magenta"), Qt::magenta},
        {tr("Dark Magenta"), Qt::darkMagenta},
        {tr("Yellow"), Qt::yellow},
        {tr("Dark Yellow"), Qt::darkYellow},
        {tr("Gray"), Qt::gray},
        {tr("Dark Gray"), Qt::darkGray},
        {tr("Light Gray"), Qt::lightGray}
    };


    m_lineWeights = {
        {"width001", 0.01},
        {"width002", 0.05},
        {"width003", 0.09},
        {"width004", 0.13},
        {"width005", 0.15},
        {"width006", 0.18},
        {"width007", 0.20},
        {"width01", 0.25},
        {"width02", 0.30},
        {"width03", 0.35},
        {"width04", 0.40},
        {"width05", 0.50},
        {"width06", 0.53},
        {"width07", 0.60},
        {"width08", 0.70},
        {"width09", 0.80},
        {"width10", 0.90},
        {"width11", 1.00},
        {"width12", 1.06},
        {"width13", 1.20},
        {"width14", 1.40},
        {"width15", 1.58},
        {"width16", 2.00},
        {"width17", 2.11}
    };
}

QList<SelectColor> StyleManager::getColors() const
{
    return m_colors;
}

QList<LineWeight> StyleManager::getLineWeights() const
{
    return m_lineWeights;
}
void StyleManager::addCustomColor(const SelectColor& color)
{
    // 1. 중복 검사 (인덱스 기반 루프 사용)
    for (int i = 0; i < m_colors.size(); ++i) {
        // 이미 완전히 동일한 RGB 값을 가진 색상이 존재한다면
        if (m_colors.at(i).color == color.color) {
            return; // 추가하지 않고 함수를 종료합니다.
        }
    }

    // 2. 중복이 없다면 리스트의 맨 끝에 추가
    m_colors.append(color);
}
