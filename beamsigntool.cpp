#include "beamsigntool.h"
#include "beamscheduletool.h"
#include "blockitem.h"
#include "cadcommands.h"
#include "caditem.h"
#include "commanddock.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "inputparser.h"
#include "layer.h"
#include "layermanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "textitem.h"
#include "tooldefaults.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QHeaderView>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QFont>
#include <QFontMetricsF>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QPainterPath>
#include <QPixmap>
#include <QSpinBox>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QtMath>

#include <cmath>

// ─────────────────────────────────────────────────────────────────────────────
// 문자 폭 / 도형
// ─────────────────────────────────────────────────────────────────────────────
// 문자 폭 정보 : 힌팅 오차를 없애려고 100배 크기로 측정한 뒤 환산 (TextItem 과 같은 포인트 단위)
static BeamSignMetrics signTextMetrics(const QString &text, double size, const QString &family)
{
    BeamSignMetrics m;
    if (text.isEmpty()) return m;
    QFont f(family);
    f.setPointSizeF(qMax(0.01, size * 100.0));   // 글자는 TextItem 처럼 포인트 크기로 그려지므로 같은 단위로 측정
    const QFontMetricsF fm(f);
    m.advance = fm.horizontalAdvance(text) / 100.0;
    const QRectF ink = fm.tightBoundingRect(text);
    m.inkLeft  = ink.left() / 100.0;
    m.inkRight = ink.right() / 100.0;
    if (m.inkRight <= m.inkLeft) {   // 측정 실패 시 진행 폭 전체
        m.inkLeft  = 0.0;
        m.inkRight = m.advance;
    }
    return m;
}

BeamSignGeom BeamSignTool::buildGeom(const BeamMarkParts &parts, const BeamSignSizes &sizes,
                                     const QString &fontFamily)
{
    return BeamSignBuilder::build(parts, sizes,
                                  signTextMetrics(parts.prefix, sizes.prefix, fontFamily),
                                  signTextMetrics(parts.type, sizes.type, fontFamily),
                                  signTextMetrics(parts.number, sizes.number, fontFamily));
}

// ─────────────────────────────────────────────────────────────────────────────
// 도면의 부호 읽기 / 번호 제안
// ─────────────────────────────────────────────────────────────────────────────
QMap<QString, int> BeamSignTool::signCountsInDrawing(MyGraphicsScene *scene)
{
    QMap<QString, int> counts;
    if (!scene) return counts;

    const QList<QGraphicsItem *> all = scene->items();
    for (int i = 0; i < all.size(); ++i) {
        BlockItem *bi = dynamic_cast<BlockItem *>(all.at(i));
        if (!bi || bi->blockName() != BeamSignBuilder::kBlockName) continue;

        BeamMarkParts p;
        const QList<CadItem *> children = bi->getItems();
        for (int k = 0; k < children.size(); ++k) {
            TextItem *ti = dynamic_cast<TextItem *>(children.at(k));
            if (!ti) continue;
            const QMap<QString, QVariant> props = ti->getProperties();
            const QString tag = props.value("tag").toString();
            const QString content = props.value("Content").toString();
            if (tag == BeamSignBuilder::kTagPrefix)      p.prefix = content;
            else if (tag == BeamSignBuilder::kTagType)   p.type = content;
            else if (tag == BeamSignBuilder::kTagNumber) p.number = content;
        }
        if (p.isValid()) counts[p.compose()] += 1;
    }
    return counts;
}

QStringList BeamSignTool::signNamesInDrawing(MyGraphicsScene *scene)
{
    return signCountsInDrawing(scene).keys();
}

QStringList BeamSignTool::allMarkNames(MyGraphicsScene *scene)
{
    QStringList names = signNamesInDrawing(scene);
    const BeamScheduleData d = BeamScheduleTool::loadData(scene);
    for (int i = 0; i < d.beams.size(); ++i) {
        const QString n = d.beams.at(i).name.trimmed();
        if (!n.isEmpty() && !names.contains(n)) names << n;
    }
    return names;
}

int BeamSignTool::suggestNumber(MyGraphicsScene *scene, const QString &prefix, const QString &type)
{
    return BeamSignText::nextNumber(allMarkNames(scene), prefix, type, BeamSignText::defaultTypes());
}

// ─────────────────────────────────────────────────────────────────────────────
// BeamSignTool
// ─────────────────────────────────────────────────────────────────────────────
BeamSignTool::BeamSignTool(const BeamSignParams &params, QObject *parent)
    : AbstractTool(parent), m_params(params)
{}

double BeamSignTool::scaleValue() const
{
    if (!m_scene) return 1.0;
    const double s = m_scene->drawingScale();
    return s > 1e-9 ? s : 1.0;
}

void BeamSignTool::activate(MyGraphicsScene *scene)
{
    if (m_scene == scene) return;
    AbstractTool::activate(scene);

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("보 부호"), CommandDock::Command);
    }
    showPrompt();
}

void BeamSignTool::deactivate()
{
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    AbstractTool::deactivate();
}

void BeamSignTool::showPrompt()
{
    IAppContext *ctx = getContext();
    if (!ctx || !ctx->commandDock()) return;
    ctx->commandDock()->showInteractivePrompt(
        tr(" 부호 %1 위치를 지정하세요 <S: 설정, 종료: 우클릭> : ").arg(m_params.parts().compose()), {});
}

void BeamSignTool::finishTool()
{
    if (m_scene) {
        m_scene->setGhostPath(QPainterPath(), false);
        m_scene->clearOverlay();
    }
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock())
        ctx->commandDock()->setPrompt(QString::fromUtf8(" Command : "));
    emit toolFinished();
}

void BeamSignTool::reopenSettings()
{
    IAppContext *ctx = getContext();
    if (!ctx || !m_scene) return;
    QWidget *parentWin = dynamic_cast<QWidget *>(ctx->commandDock());
    BeamSignDialog dlg(m_params, m_scene, ctx->layerManager(), parentWin);
    if (dlg.exec() == QDialog::Accepted) {
        m_params = dlg.params();
        saveParams(m_scene, m_params);
    }
    showPrompt();
}

bool BeamSignTool::processCommand(const QString &command)
{
    IAppContext *ctx = getContext();
    if (!ctx) return false;

    const QString cmd = command.trimmed().toLower();
    if (cmd == QLatin1String("s") || cmd == QLatin1String("set")) {
        reopenSettings();
        return true;
    }
    if (!ctx->parser()) return false;
    QPointF result;
    if (ctx->parser()->parseCoordinate(command.trimmed(), QPointF(0, 0), QPointF(0, 0),
                                       false, result)) {
        mousePress(result, Qt::LeftButton);
        return true;
    }
    return false;
}

void BeamSignTool::keyPress(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape)
        finishTool();
    else
        AbstractTool::keyPress(event);
}

void BeamSignTool::mousePress(QPointF pos, Qt::MouseButton button)
{
    if (!m_scene) return;
    if (button == Qt::RightButton) {
        finishTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    if (ctx && ctx->snapManager())
        snapped = ctx->snapManager()->snapPoint(pos, m_scene, QPointF()).snappedPos;
    place(snapped);
}

void BeamSignTool::mouseMove(QPointF pos)
{
    if (!m_scene || m_scene->views().isEmpty()) return;

    IAppContext *ctx = getContext();
    QPointF snapped = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        auto r = ctx->snapManager()->snapPoint(pos, m_scene, QPointF());
        snapped = r.snappedPos;
        snapType = r.type;
    }

    // 미리보기 : 타원형 외곽 (도면 축척 적용)
    const double s = scaleValue();
    const BeamSignGeom g = buildGeom(m_params.parts(), m_params.sizes,
                                     ctx ? ctx->currentFontFamily() : QString());
    QPainterPath path;
    for (int i = 0; i < g.lines.size(); ++i) {
        path.moveTo(snapped.x() + g.lines.at(i).x1() * s, snapped.y() + g.lines.at(i).y1() * s);
        path.lineTo(snapped.x() + g.lines.at(i).x2() * s, snapped.y() + g.lines.at(i).y2() * s);
    }
    for (int i = 0; i < g.arcs.size(); ++i) {
        const BeamSignArc &a = g.arcs.at(i);
        const int seg = 12;
        for (int k = 0; k <= seg; ++k) {
            const double ang = qDegreesToRadians(a.startAngle + a.spanAngle * k / seg);
            const QPointF q(snapped.x() + (a.center.x() + a.radius * std::cos(ang)) * s,
                            snapped.y() + (a.center.y() + a.radius * std::sin(ang)) * s);
            if (k == 0) path.moveTo(q);
            else        path.lineTo(q);
        }
    }
    updateGhostPreview(path, m_params.outLayer);
    m_scene->setSnapMarker(snapped, snapType);
}

void BeamSignTool::ensureLayer(IAppContext *ctx, const QString &name, const QColor &color)
{
    if (!ctx || !ctx->layerManager() || name.isEmpty()) return;
    if (ctx->layerManager()->getLayer(name)) return;
    Layer layer(name, color, 0.25, QStringLiteral("Continuous"),
                false, true, true, false);
    ctx->layerManager()->addLayer(layer);
}

// 일람표 목록에 없는 부호는 빈 줄로 등록 (철근 데이터는 일람표에서 입력)
void BeamSignTool::registerInList(const QString &name)
{
    BeamScheduleData d = BeamScheduleTool::loadData(m_scene);
    for (int i = 0; i < d.beams.size(); ++i) {
        if (d.beams.at(i).name.trimmed().compare(name, Qt::CaseInsensitive) == 0)
            return;   // 이미 있는 부호는 기존 값을 따른다
    }
    int slot = -1;
    for (int i = 0; i < d.beams.size(); ++i) {
        if (d.beams.at(i).isEmpty()) { slot = i; break; }
    }
    if (slot < 0) {
        d.beams.append(BeamMark());
        slot = d.beams.size() - 1;
    }
    d.beams[slot].name = name;
    if (m_params.setSize) {
        d.beams[slot].width = m_params.width;
        d.beams[slot].depth = m_params.depth;
    }
    BeamScheduleTool::saveData(m_scene, d);
}

// 블록 BEAMSIGN 삽입 (속성 PREFIX / TYPE / NO)
void BeamSignTool::place(const QPointF &pos)
{
    IAppContext *ctx = getContext();
    if (!m_scene || !ctx || !ctx->entityFactory()) return;

    const BeamMarkParts parts = m_params.parts();
    if (!parts.isValid()) return;

    ensureLayer(ctx, m_params.outLayer,  QColor(255, 255, 0));
    ensureLayer(ctx, m_params.textLayer, QColor(0, 255, 0));
    const QPen outPen  = AbstractTool::layerPenFor(ctx, m_params.outLayer);
    const QPen textPen = AbstractTool::layerPenFor(ctx, m_params.textLayer);
    const QString family = ctx->currentFontFamily();

    const BeamSignGeom g = buildGeom(parts, m_params.sizes, family);
    EntityFactory *ef = ctx->entityFactory();

    QList<CadItem *> children;
    for (int i = 0; i < g.lines.size(); ++i) {
        CadItem *ci = dynamic_cast<CadItem *>(ef->createLine(g.lines.at(i), outPen));
        if (ci) { ci->setLayerName(m_params.outLayer); children.append(ci); }
    }
    for (int i = 0; i < g.arcs.size(); ++i) {
        const BeamSignArc &a = g.arcs.at(i);
        CadItem *ci = dynamic_cast<CadItem *>(
            ef->createArc(a.center, a.radius, a.startAngle, a.spanAngle, outPen));
        if (ci) { ci->setLayerName(m_params.outLayer); children.append(ci); }
    }
    for (int i = 0; i < g.texts.size(); ++i) {
        const BeamSignLabel &t = g.texts.at(i);
        QFont font;
        font.setFamily(family);
        font.setPointSizeF(t.size);
        CadItem *ci = dynamic_cast<CadItem *>(
            ef->createText(t.text, t.pos, font, textPen.color(), 0.0, 1));
        if (!ci) continue;
        ci->setLayerName(m_params.textLayer);
        ci->setProperty(QStringLiteral("isAttribute"), true);
        ci->setProperty(QStringLiteral("tag"), t.tag);
        ci->setProperty(QStringLiteral("prompt"), t.prompt);
        children.append(ci);
    }

    const double s = scaleValue();
    QGraphicsItem *blockGi = ef->createBlock(children, MyVector(pos),
                                             BeamSignBuilder::kBlockName, s, s, 0.0);
    CadItem *block = dynamic_cast<CadItem *>(blockGi);
    if (!block) return;
    block->setLayerName(m_params.outLayer);
    block->setPen(outPen);
    blockGi->setParentItem(m_scene->getLayerRoot(m_params.outLayer));
    if (blockGi->scene() == m_scene) m_scene->removeItem(blockGi);
    if (ctx->undoStack()) ctx->undoStack()->push(new AddEntityCommand(m_scene, blockGi));
    else                  m_scene->addItem(blockGi);

    registerInList(parts.compose());

    if (ctx->commandDock())
        ctx->commandDock()->logMessage(tr("보 부호 %1 배치").arg(parts.compose()), CommandDock::Info);

    // 다음 번호
    if (m_params.autoIncrement)
        m_params.number = suggestNumber(m_scene, m_params.prefix, m_params.type);
    else
        m_params.number += 1;
    saveParams(m_scene, m_params);
    showPrompt();
}

BeamSignParams BeamSignTool::loadParams(MyGraphicsScene *scene)
{
    BeamSignParams p;
    ToolDefaults d("BeamSignTool", scene);
    p.prefix        = d.getString("Prefix", p.prefix);
    p.type          = d.getString("Type", p.type);
    p.number        = qRound(d.getDouble("Number", p.number));
    p.sizes.prefix  = d.getDouble("PrefixSize", p.sizes.prefix);
    p.sizes.type    = d.getDouble("TypeSize", p.sizes.type);
    p.sizes.number  = d.getDouble("NumberSize", p.sizes.number);
    p.autoIncrement = d.getBool("AutoIncrement", p.autoIncrement);
    p.setSize       = d.getBool("SetSize", p.setSize);
    p.width         = d.getDouble("Width", p.width);
    p.depth         = d.getDouble("Depth", p.depth);
    p.outLayer      = d.getString("OutLayer", p.outLayer);
    p.textLayer     = d.getString("TextLayer", p.textLayer);
    return p;
}

void BeamSignTool::saveParams(MyGraphicsScene *scene, const BeamSignParams &p)
{
    ToolDefaults d("BeamSignTool", scene);
    d.put("Prefix", p.prefix);
    d.put("Type", p.type);
    d.put("Number", p.number);
    d.put("PrefixSize", p.sizes.prefix);
    d.put("TypeSize", p.sizes.type);
    d.put("NumberSize", p.sizes.number);
    d.put("AutoIncrement", p.autoIncrement);
    d.put("SetSize", p.setSize);
    d.put("Width", p.width);
    d.put("Depth", p.depth);
    d.put("OutLayer", p.outLayer);
    d.put("TextLayer", p.textLayer);
}

// ─────────────────────────────────────────────────────────────────────────────
// BeamSignDialog
// ─────────────────────────────────────────────────────────────────────────────
void BeamSignDialog::populateLayerCombo(QComboBox *combo, LayerManager *mgr, const QString &selected)
{
    combo->clear();
    if (mgr) {
        const QList<Layer> layers = mgr->getAllLayers();
        for (int i = 0; i < layers.size(); ++i) {
            QPixmap px(16, 16);
            px.fill(layers.at(i).color());
            combo->addItem(QIcon(px), layers.at(i).name(), layers.at(i).name());
        }
    }
    int idx = combo->findData(selected);
    if (idx < 0 && !selected.isEmpty()) {   // 도면에 아직 없는 레이어 (그릴 때 생성됨)
        combo->addItem(selected, selected);
        idx = combo->count() - 1;
    }
    combo->setCurrentIndex(idx >= 0 ? idx : 0);
}

BeamSignDialog::BeamSignDialog(const BeamSignParams &params, MyGraphicsScene *scene,
                               LayerManager *layerMgr, QWidget *parent)
    : QDialog(parent), m_scene(scene)
{
    setWindowTitle(tr("보 부호"));

    // 층 구분 / 종류 : 목록에서 고르거나 직접 입력
    m_comboPrefix = new QComboBox(this);
    m_comboPrefix->setEditable(true);
    m_comboPrefix->addItems(BeamSignText::defaultPrefixes());
    m_comboPrefix->setCurrentText(params.prefix);

    m_comboType = new QComboBox(this);
    m_comboType->setEditable(true);
    m_comboType->addItems(BeamSignText::defaultTypes());
    m_comboType->setCurrentText(params.type);

    m_spinNumber = new QSpinBox(this);
    m_spinNumber->setRange(1, 99999);
    m_spinNumber->setValue(params.number);

    // 각 입력 뒤의 글자 크기 (축척 1 기준 글자 높이)
    m_spinPrefixSize = new QDoubleSpinBox(this);
    m_spinTypeSize   = new QDoubleSpinBox(this);
    m_spinNumberSize = new QDoubleSpinBox(this);
    QDoubleSpinBox *sizeSpins[3] = {m_spinPrefixSize, m_spinTypeSize, m_spinNumberSize};
    for (int i = 0; i < 3; ++i) {
        sizeSpins[i]->setDecimals(1);
        sizeSpins[i]->setRange(0.5, 20.0);
        sizeSpins[i]->setSingleStep(0.5);
    }
    m_spinPrefixSize->setValue(params.sizes.prefix);
    m_spinTypeSize->setValue(params.sizes.type);
    m_spinNumberSize->setValue(params.sizes.number);

    QWidget *prefixRow = new QWidget(this);
    QHBoxLayout *prefixLay = new QHBoxLayout(prefixRow);
    prefixLay->setContentsMargins(0, 0, 0, 0);
    prefixLay->addWidget(m_comboPrefix, 1);
    prefixLay->addWidget(new QLabel(tr("글자 크기"), prefixRow));
    prefixLay->addWidget(m_spinPrefixSize);

    QWidget *typeRow = new QWidget(this);
    QHBoxLayout *typeLay = new QHBoxLayout(typeRow);
    typeLay->setContentsMargins(0, 0, 0, 0);
    typeLay->addWidget(m_comboType, 1);
    typeLay->addWidget(new QLabel(tr("글자 크기"), typeRow));
    typeLay->addWidget(m_spinTypeSize);

    QWidget *numberRow = new QWidget(this);
    QHBoxLayout *numberLay = new QHBoxLayout(numberRow);
    numberLay->setContentsMargins(0, 0, 0, 0);
    numberLay->addWidget(m_spinNumber, 1);
    numberLay->addWidget(new QLabel(tr("글자 크기"), numberRow));
    numberLay->addWidget(m_spinNumberSize);
    m_checkAuto = new QCheckBox(tr("번호 자동 증가 (같은 층 구분·종류의 다음 번호)"), this);
    m_checkAuto->setChecked(params.autoIncrement);

    m_labelPreview = new QLabel(this);
    QFont pf = m_labelPreview->font();
    pf.setPointSizeF(pf.pointSizeF() * 1.6);
    pf.setBold(true);
    m_labelPreview->setFont(pf);
    m_labelPreview->setAlignment(Qt::AlignCenter);

    QGroupBox *markBox = new QGroupBox(tr("부호"), this);
    QFormLayout *markForm = new QFormLayout(markBox);
    markForm->addRow(tr("층 구분"), prefixRow);
    markForm->addRow(tr("종류"), typeRow);
    markForm->addRow(tr("번호"), numberRow);
    markForm->addRow(m_checkAuto);
    markForm->addRow(m_labelPreview);

    // 일람표에 보 크기도 같이 등록 (선택)
    m_checkSize = new QCheckBox(tr("일람표에 보 크기도 등록"), this);
    m_checkSize->setChecked(params.setSize);
    m_spinWidth = new QDoubleSpinBox(this);
    m_spinDepth = new QDoubleSpinBox(this);
    QDoubleSpinBox *dims[2] = {m_spinWidth, m_spinDepth};
    for (int i = 0; i < 2; ++i) {
        dims[i]->setDecimals(0);
        dims[i]->setRange(100.0, 5000.0);
        dims[i]->setSingleStep(50.0);
    }
    m_spinWidth->setValue(params.width);
    m_spinDepth->setValue(params.depth);

    QGroupBox *sizeBox = new QGroupBox(tr("보 크기 (선택)"), this);
    QFormLayout *sizeForm = new QFormLayout(sizeBox);
    sizeForm->addRow(m_checkSize);
    sizeForm->addRow(tr("폭 b"), m_spinWidth);
    sizeForm->addRow(tr("춤 D"), m_spinDepth);

    // 레이어
    m_comboOut  = new QComboBox(this);
    m_comboText = new QComboBox(this);
    populateLayerCombo(m_comboOut,  layerMgr, params.outLayer);
    populateLayerCombo(m_comboText, layerMgr, params.textLayer);
    QGroupBox *layerBox = new QGroupBox(tr("레이어"), this);
    QFormLayout *layerForm = new QFormLayout(layerBox);
    layerForm->addRow(tr("외곽"), m_comboOut);
    layerForm->addRow(tr("문자"), m_comboText);

    QDialogButtonBox *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(markBox);
    lay->addWidget(sizeBox);
    lay->addWidget(layerBox);
    lay->addWidget(buttons);

    connect(m_comboPrefix, &QComboBox::currentTextChanged, this, &BeamSignDialog::onKeyChanged);
    connect(m_comboType, &QComboBox::currentTextChanged, this, &BeamSignDialog::onKeyChanged);
    connect(m_spinNumber, &QSpinBox::valueChanged, this, &BeamSignDialog::updatePreview);
    connect(m_checkAuto, &QCheckBox::toggled, this, &BeamSignDialog::onAutoToggled);

    if (params.autoIncrement) onKeyChanged();
    updatePreview();
}

void BeamSignDialog::onKeyChanged()
{
    if (m_checkAuto->isChecked()) {
        const int n = BeamSignTool::suggestNumber(m_scene, m_comboPrefix->currentText().trimmed(),
                                                  m_comboType->currentText().trimmed());
        m_spinNumber->setValue(n);
    }
    updatePreview();
}

void BeamSignDialog::onAutoToggled(bool on)
{
    if (on) onKeyChanged();
}

void BeamSignDialog::updatePreview()
{
    m_labelPreview->setText(m_comboPrefix->currentText().trimmed() + m_comboType->currentText().trimmed()
                            + QString::number(m_spinNumber->value()));
}

BeamSignParams BeamSignDialog::params() const
{
    BeamSignParams p;
    p.prefix        = m_comboPrefix->currentText().trimmed();
    p.type          = m_comboType->currentText().trimmed();
    p.number        = m_spinNumber->value();
    p.sizes.prefix  = m_spinPrefixSize->value();
    p.sizes.type    = m_spinTypeSize->value();
    p.sizes.number  = m_spinNumberSize->value();
    p.autoIncrement = m_checkAuto->isChecked();
    p.setSize       = m_checkSize->isChecked();
    p.width         = m_spinWidth->value();
    p.depth         = m_spinDepth->value();
    p.outLayer      = m_comboOut->currentData().toString();
    p.textLayer     = m_comboText->currentData().toString();
    return p;
}

// ─────────────────────────────────────────────────────────────────────────────
// BeamSignImportDialog
// ─────────────────────────────────────────────────────────────────────────────
BeamSignImportDialog::BeamSignImportDialog(const QStringList &existingNames, MyGraphicsScene *scene,
                                           QWidget *parent)
    : QDialog(parent), m_existing(existingNames), m_scene(scene),
      m_knownTypes(BeamSignText::defaultTypes())
{
    setWindowTitle(tr("구조도에서 보 부호 가져오기"));
    resize(640, 560);

    // 구조도 파일 목록
    m_listFiles = new QListWidget(this);
    m_listFiles->setMaximumHeight(110);
    QPushButton *btnAdd = new QPushButton(tr("구조도 파일 추가..."), this);
    QPushButton *btnRemove = new QPushButton(tr("선택 파일 제거"), this);
    m_checkCurrent = new QCheckBox(tr("현재 도면의 부호 포함"), this);
    m_checkCurrent->setChecked(true);

    QVBoxLayout *fileBtnLay = new QVBoxLayout;
    fileBtnLay->addWidget(btnAdd);
    fileBtnLay->addWidget(btnRemove);
    fileBtnLay->addStretch(1);
    QHBoxLayout *fileLay = new QHBoxLayout;
    fileLay->addWidget(m_listFiles, 1);
    fileLay->addLayout(fileBtnLay);

    // 층 구분 필터 + 전체 선택/해제
    m_comboFloor = new QComboBox(this);
    QPushButton *btnAll = new QPushButton(tr("전체 선택"), this);
    QPushButton *btnNone = new QPushButton(tr("전체 해제"), this);
    QHBoxLayout *filterLay = new QHBoxLayout;
    filterLay->addWidget(new QLabel(tr("층 구분"), this));
    filterLay->addWidget(m_comboFloor);
    filterLay->addStretch(1);
    filterLay->addWidget(btnAll);
    filterLay->addWidget(btnNone);

    // 부호 표
    m_table = new QTableWidget(this);
    m_table->setColumnCount(4);
    m_table->setHorizontalHeaderLabels(QStringList() << tr("부호") << tr("도면 수량") << tr("상태") << tr("출처"));
    m_table->horizontalHeader()->setStretchLastSection(true);
    m_table->verticalHeader()->setVisible(false);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);

    m_labelInfo = new QLabel(this);

    QDialogButtonBox *buttons = new QDialogButtonBox(this);
    QPushButton *okBtn = buttons->addButton(tr("가져오기"), QDialogButtonBox::AcceptRole);
    Q_UNUSED(okBtn);
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    QVBoxLayout *lay = new QVBoxLayout(this);
    lay->addWidget(new QLabel(tr("구조도(.cdb) 파일을 추가하면 그 안의 보 부호를 읽어 옵니다. 층별 일람표는 층 구분으로 걸러 가져오세요."), this));
    lay->addLayout(fileLay);
    lay->addWidget(m_checkCurrent);
    lay->addLayout(filterLay);
    lay->addWidget(m_table, 1);
    lay->addWidget(m_labelInfo);
    lay->addWidget(buttons);

    connect(btnAdd, &QPushButton::clicked, this, &BeamSignImportDialog::onAddFiles);
    connect(btnRemove, &QPushButton::clicked, this, &BeamSignImportDialog::onRemoveFile);
    connect(m_checkCurrent, &QCheckBox::toggled, this, &BeamSignImportDialog::onSourceChanged);
    connect(m_comboFloor, &QComboBox::currentIndexChanged, this, &BeamSignImportDialog::onFilterChanged);
    connect(btnAll, &QPushButton::clicked, this, &BeamSignImportDialog::onSelectAll);
    connect(btnNone, &QPushButton::clicked, this, &BeamSignImportDialog::onSelectNone);
    connect(m_table, &QTableWidget::itemChanged, this, &BeamSignImportDialog::onItemChanged);

    rescan();
}

void BeamSignImportDialog::onAddFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("구조도 선택"), QString(), tr("AutoDraft 도면 (*.cdb);;모든 파일 (*)"));
    for (int i = 0; i < files.size(); ++i) {
        bool exists = false;
        for (int k = 0; k < m_listFiles->count(); ++k)
            if (m_listFiles->item(k)->data(Qt::UserRole).toString() == files.at(i)) exists = true;
        if (exists) continue;
        QListWidgetItem *it = new QListWidgetItem(QFileInfo(files.at(i)).fileName(), m_listFiles);
        it->setData(Qt::UserRole, files.at(i));
        it->setToolTip(files.at(i));
    }
    rescan();
}

void BeamSignImportDialog::onRemoveFile()
{
    delete m_listFiles->takeItem(m_listFiles->currentRow());
    rescan();
}

void BeamSignImportDialog::onSourceChanged()
{
    rescan();
}

// 파일과 현재 도면을 다시 읽어 {부호 → 수량/출처} 갱신
void BeamSignImportDialog::rescan()
{
    m_found.clear();
    QStringList errors;

    for (int i = 0; i < m_listFiles->count(); ++i) {
        const QString path = m_listFiles->item(i)->data(Qt::UserRole).toString();
        QString err;
        const QMap<QString, int> counts = BeamSignReader::readCdb(path, &err);
        if (!err.isEmpty()) {
            errors << QStringLiteral("%1 : %2").arg(QFileInfo(path).fileName(), err);
            continue;
        }
        for (QMap<QString, int>::const_iterator it = counts.constBegin(); it != counts.constEnd(); ++it) {
            Found &f = m_found[it.key()];
            f.count += it.value();
            const QString src = QFileInfo(path).fileName();
            if (!f.sources.contains(src)) f.sources << src;
        }
    }
    if (m_checkCurrent->isChecked()) {
        const QMap<QString, int> counts = BeamSignTool::signCountsInDrawing(m_scene);
        for (QMap<QString, int>::const_iterator it = counts.constBegin(); it != counts.constEnd(); ++it) {
            Found &f = m_found[it.key()];
            f.count += it.value();
            const QString src = tr("(현재 도면)");
            if (!f.sources.contains(src)) f.sources << src;
        }
    }

    // 새로 나타난 부호는 기본 체크 (이미 일람표에 있는 부호는 해제)
    QMap<QString, bool> newChecked;
    for (QMap<QString, Found>::const_iterator it = m_found.constBegin(); it != m_found.constEnd(); ++it) {
        const bool isNew = !m_existing.contains(it.key(), Qt::CaseInsensitive);
        newChecked[it.key()] = m_checked.contains(it.key()) ? m_checked.value(it.key()) : isNew;
    }
    m_checked = newChecked;

    // 층 구분 콤보 갱신 (이전 선택 유지)
    const QString keepFloor = m_comboFloor->currentData().toString();
    const bool hadKeep = m_comboFloor->count() > 0;
    QStringList prefixes;
    for (QMap<QString, Found>::const_iterator it = m_found.constBegin(); it != m_found.constEnd(); ++it) {
        const QString pf = BeamSignText::parse(it.key(), m_knownTypes).prefix;
        if (!prefixes.contains(pf)) prefixes << pf;
    }
    prefixes = BeamSignReader::naturalSorted(prefixes);

    m_comboFloor->blockSignals(true);
    m_comboFloor->clear();
    m_comboFloor->addItem(tr("전체"), QStringLiteral("*"));
    for (int i = 0; i < prefixes.size(); ++i)
        m_comboFloor->addItem(prefixes.at(i).isEmpty() ? tr("(없음)") : prefixes.at(i), prefixes.at(i));
    int idx = hadKeep ? m_comboFloor->findData(keepFloor) : 0;
    m_comboFloor->setCurrentIndex(idx >= 0 ? idx : 0);
    m_comboFloor->blockSignals(false);

    rebuildTable();

    if (!errors.isEmpty())
        QMessageBox::warning(this, tr("보 부호 가져오기"),
                             tr("읽지 못한 파일이 있습니다.\n\n%1").arg(errors.join(QLatin1Char('\n'))));
}

void BeamSignImportDialog::onFilterChanged()
{
    rebuildTable();
}

// 현재 층 구분 필터에 맞는 부호를 자연 정렬해 표에 표시
void BeamSignImportDialog::rebuildTable()
{
    m_building = true;
    m_table->setRowCount(0);

    const QString floor = m_comboFloor->currentData().toString();
    const QStringList names = BeamSignReader::naturalSorted(m_found.keys());
    int newCount = 0;
    int shown = 0;
    for (int i = 0; i < names.size(); ++i) {
        const QString &name = names.at(i);
        const QString pf = BeamSignText::parse(name, m_knownTypes).prefix;
        if (floor != QLatin1String("*") && pf != floor) continue;

        const Found &f = m_found.value(name);
        const bool isNew = !m_existing.contains(name, Qt::CaseInsensitive);
        if (isNew) ++newCount;
        ++shown;

        const int row = m_table->rowCount();
        m_table->insertRow(row);
        QTableWidgetItem *nameItem = new QTableWidgetItem(name);
        nameItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        nameItem->setCheckState(m_checked.value(name) ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(row, 0, nameItem);
        m_table->setItem(row, 1, new QTableWidgetItem(QString::number(f.count)));
        m_table->setItem(row, 2, new QTableWidgetItem(isNew ? tr("신규") : tr("이미 있음")));
        m_table->setItem(row, 3, new QTableWidgetItem(f.sources.join(QStringLiteral(", "))));
    }
    m_table->resizeColumnToContents(0);
    m_building = false;

    m_labelInfo->setText(tr("표시 %1개 (신규 %2개, 이미 있음 %3개)").arg(shown).arg(newCount).arg(shown - newCount));
}

void BeamSignImportDialog::onItemChanged()
{
    if (m_building) return;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *it = m_table->item(r, 0);
        if (it) m_checked[it->text()] = (it->checkState() == Qt::Checked);
    }
}

void BeamSignImportDialog::onSelectAll()
{
    m_building = true;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *it = m_table->item(r, 0);
        if (it) { it->setCheckState(Qt::Checked); m_checked[it->text()] = true; }
    }
    m_building = false;
}

void BeamSignImportDialog::onSelectNone()
{
    m_building = true;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *it = m_table->item(r, 0);
        if (it) { it->setCheckState(Qt::Unchecked); m_checked[it->text()] = false; }
    }
    m_building = false;
}

QStringList BeamSignImportDialog::selectedNames() const
{
    QStringList out;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        QTableWidgetItem *it = m_table->item(r, 0);
        if (it && it->checkState() == Qt::Checked) out << it->text();
    }
    return BeamSignReader::naturalSorted(out);
}

// ─────────────────────────────────────────────────────────────────────────────
// BeamSignCommand
// ─────────────────────────────────────────────────────────────────────────────
CommandBase *BeamSignCommand::createCommand(IAppContext *c)
{
    return new BeamSignCommand(c);
}

BeamSignCommand::BeamSignCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx)
{}

void BeamSignCommand::execute()
{
    if (!m_ctx) return;
    MyGraphicsScene *scene = m_ctx->activeScene();
    BeamSignParams params = BeamSignTool::loadParams(scene);
    QWidget *parentWin = dynamic_cast<QWidget *>(m_ctx->commandDock());
    BeamSignDialog dlg(params, scene, m_ctx->layerManager(), parentWin);
    if (dlg.exec() != QDialog::Accepted) return;
    params = dlg.params();
    if (!params.parts().isValid()) return;
    BeamSignTool::saveParams(scene, params);
    m_ctx->startTool(new BeamSignTool(params));
}
