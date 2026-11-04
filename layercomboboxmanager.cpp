#include "layercomboboxmanager.h"
#include "appcontext.h"
#include "linetypemanager.h" // AppContext에 등록된 매니저
#include "stylemanager.h"    // 색상, 굵기 데이터 매니저

#include <QPixmap>
#include <QIcon>

// 선종류 미리보기 아이콘 크기 (기존 icons/*.png 와 동일한 32x12)
static const QSize kLtIconSize(32, 12);

// --- 라인타입 콤보박스 ---
LineTypeComboBox::LineTypeComboBox(AppContext *context, QWidget *parent)
    : QComboBox(parent), m_appContext(context) {}

void LineTypeComboBox::populate(LineTypeManager::LinetypeGroup group) {
    if (!m_appContext || !m_appContext->lineTypeManager()) return;
    LineTypeManager *ltm = m_appContext->lineTypeManager();

    clear();
    setIconSize(kLtIconSize);

    // 어느 그룹이든 기본값으로 실선(Continuous)을 맨 위에 둔다. 아이콘도 QPainter 로 그린다.
    addItem(LineTypeManager::makePreviewIcon(QVector<qreal>(), kLtIconSize), "Continuous", "11.0");

    // AutoDraft·AutoCAD 호환 구분 없이 모두 대시 패턴에서 아이콘을 그려 넣는다.
    const QList<LineType> linetypeList = ltm->getLinetypesInGroup(group);
    for (int i = 0; i < linetypeList.size(); ++i) {
        const LineType &lt = linetypeList.at(i);
        addItem(ltm->previewIconFor(lt.name, kLtIconSize), lt.name, lt.pattern);
    }

    setCurrentIndex(0); // Continuous
}

// --- 라인두께 콤보박스 ---
LineWidthComboBox::LineWidthComboBox(AppContext *context, QWidget *parent)
    : QComboBox(parent), m_appContext(context) {}

void LineWidthComboBox::populate() {
    if (!m_appContext || !m_appContext->styleManager()) return;

    QList<LineWeight> weightList = m_appContext->styleManager()->getLineWeights();

    // 인덱스 기반 루프 사용
    for(int i = 0; i < weightList.size(); ++i){
        const LineWeight &lw = weightList.at(i);
        QIcon icon(":/icons/" + lw.name);
        addItem(icon, (QString::number(lw.weight, 'f', 2) + " mm"));
    }
}

// --- 컬러 콤보박스 ---
ColorComboBox::ColorComboBox(AppContext *context, QWidget *parent)
    : QComboBox(parent), m_appContext(context) {}

void ColorComboBox::populate() {
    if (!m_appContext || !m_appContext->styleManager()) return;

    QList<SelectColor> colorList = m_appContext->styleManager()->getColors();

    // 인덱스 기반 루프 사용
    for (int i = 0; i < colorList.size(); ++i) {
        const SelectColor &lc = colorList.at(i);
        QPixmap pixmap(20, 20);
        pixmap.fill(lc.color);
        QIcon icon(pixmap);
        addItem(icon, lc.name);
    }
}
