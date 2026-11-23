#include "model3dwindow.h"
#include "iappcontext.h"
#include "buildingmodel3d.h"
#include "drawsubwindow.h"
#include "model3dview.h"
#include "model3dexport.h"
#include "mechmodel3d.h"
#include "mygraphicsscene.h"
#include "isodrawtool.h"

#include <QDir>
#include <QMdiArea>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QStandardPaths>

#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QComboBox>
#include <QSignalBlocker>
#include <QPushButton>
#include <QVBoxLayout>

Model3DWindow::Model3DWindow(IAppContext *context, QWidget *parent)
    : QWidget(parent, Qt::Window), m_appContext(context)
{
    setWindowTitle(tr("3D 보기"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1000, 720);

    QVBoxLayout *mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    QHBoxLayout *bar = new QHBoxLayout();
    QPushButton *refresh = new QPushButton(tr("새로고침"), this);
    refresh->setToolTip(tr("도면을 고친 뒤 3D 모델을 다시 만듭니다"));
    QPushButton *iso   = new QPushButton(tr("등각"), this);
    QPushButton *top   = new QPushButton(tr("평면"), this);
    QPushButton *front = new QPushButton(tr("정면"), this);
    QPushButton *side  = new QPushButton(tr("측면"), this);
    bar->addWidget(refresh);
    bar->addSpacing(12);
    bar->addWidget(iso);
    bar->addWidget(top);
    bar->addWidget(front);
    bar->addWidget(side);
    bar->addSpacing(12);
    QPushButton *exportObj = new QPushButton(tr("OBJ 내보내기"), this);
    exportObj->setToolTip(tr("3D 모델을 OBJ 파일(단위 m)로 저장합니다. Blender·SketchUp 등에서 열 수 있습니다"));
    QPushButton *blender = new QPushButton(tr("Blender로 열기"), this);
    blender->setToolTip(tr("3D 모델을 Blender 에서 엽니다"));
    bar->addWidget(exportObj);
    bar->addWidget(blender);
    QPushButton *drawIso = new QPushButton(tr("도면에 그리기"), this);
    drawIso->setToolTip(tr("지금 화면 방향(또는 KS 등각도)으로 보이는 부품을 도면 선으로 그립니다 (Blender 로 구멍·파내기 적용)"));
    bar->addWidget(drawIso);
    m_drawIso = drawIso;
    m_drawIso->setVisible(false);
    connect(drawIso, &QPushButton::clicked, this, &Model3DWindow::onDrawIso);
    bar->addSpacing(12);
    m_precisionLabel = new QLabel(tr("정밀도"), this);
    m_precision = new QComboBox(this);
    m_precision->addItem(tr("보통 (원 64분할)"), 64);
    m_precision->addItem(tr("높음 (128분할)"), 128);
    m_precision->addItem(tr("매우 높음 (256분할)"), 256);
    m_precision->setToolTip(tr("원기둥·구를 몇 조각 다각형으로 만들지 정합니다.\n"
                               "간섭 검사 오차: Ø20 기준 64분할 0.012 mm · 128분할 0.003 mm · 256분할 0.0008 mm\n"
                               "높을수록 파일이 커지고 느려집니다."));
    {
        const int idx = m_precision->findData(MechModel3D::radialSteps());
        m_precision->setCurrentIndex(idx >= 0 ? idx : 0);
    }
    bar->addWidget(m_precisionLabel);
    bar->addWidget(m_precision);
    m_precisionLabel->setVisible(false);
    m_precision->setVisible(false);
    bar->addSpacing(12);
    m_status = new QLabel(this);
    bar->addWidget(m_status, 1);
    mainLayout->addLayout(bar);

    // 기계용: 왼쪽 부품 체크 목록 + 오른쪽 3D 화면
    QHBoxLayout *body = new QHBoxLayout();
    m_partPanel = new QWidget(this);
    m_partPanel->setFixedWidth(200);
    QVBoxLayout *partLay = new QVBoxLayout(m_partPanel);
    partLay->setContentsMargins(0, 0, 0, 0);
    partLay->addWidget(new QLabel(tr("부품"), m_partPanel));
    QHBoxLayout *allRow = new QHBoxLayout();
    QPushButton *allBtn  = new QPushButton(tr("전체"), m_partPanel);
    QPushButton *noneBtn = new QPushButton(tr("해제"), m_partPanel);
    allRow->addWidget(allBtn);
    allRow->addWidget(noneBtn);
    partLay->addLayout(allRow);
    m_partList = new QListWidget(m_partPanel);
    m_partList->setToolTip(tr("체크한 부품만 3D 화면·OBJ 내보내기·Blender 로 보냅니다"));
    partLay->addWidget(m_partList, 1);
    m_partPanel->setVisible(false);
    body->addWidget(m_partPanel);

    m_view = new Model3DView(this);
    body->addWidget(m_view, 1);
    mainLayout->addLayout(body, 1);

    connect(m_partList, &QListWidget::itemChanged, this, &Model3DWindow::onPartItemChanged);
    connect(allBtn, &QPushButton::clicked, this, &Model3DWindow::onPartsAll);
    connect(noneBtn, &QPushButton::clicked, this, &Model3DWindow::onPartsNone);

    QLabel *help = new QLabel(tr("왼쪽 드래그: 회전 · 오른쪽/가운데 드래그: 이동 · 휠: 확대/축소"), this);
    mainLayout->addWidget(help);

    connect(refresh, &QPushButton::clicked, this, &Model3DWindow::onRefresh);
    connect(iso, &QPushButton::clicked, this, &Model3DWindow::onIso);
    connect(top, &QPushButton::clicked, this, &Model3DWindow::onTop);
    connect(front, &QPushButton::clicked, this, &Model3DWindow::onFront);
    connect(side, &QPushButton::clicked, this, &Model3DWindow::onSide);
    connect(exportObj, &QPushButton::clicked, this, &Model3DWindow::onExportObj);
    connect(blender, &QPushButton::clicked, this, &Model3DWindow::onOpenBlender);
    connect(m_precision, &QComboBox::currentIndexChanged, this, &Model3DWindow::onPrecisionChanged);
}

void Model3DWindow::onPrecisionChanged(int index)
{
    if (index < 0)
        return;
    MechModel3D::setRadialSteps(m_precision->itemData(index).toInt());
    if (m_mode == Mechanical)
        showDrawing(nullptr, false);
}

Model3DWindow::~Model3DWindow() {}

QPointer<Model3DWindow> Model3DWindow::s_instance;

Model3DWindow *Model3DWindow::existing()
{
    return s_instance.data();
}

void Model3DWindow::onDrawIso()
{
    if (!m_appContext)
        return;
    // 도면 창으로 돌아가 도구 시작 (설정창 기본값 = 이 창의 현재 화면)
    if (m_appContext->mdiArea() && m_appContext->mdiArea()->window()) {
        m_appContext->mdiArea()->window()->raise();
        m_appContext->mdiArea()->window()->activateWindow();
    }
    m_appContext->startTool(new IsoDrawTool(nullptr, true));
}

Model3DWindow *Model3DWindow::instance(IAppContext *context, QWidget *parent)
{
    if (!s_instance)
        s_instance = new Model3DWindow(context, parent);
    return s_instance.data();
}

void Model3DWindow::showDrawing(DrawSubWindow *drawing, bool resetCamera)
{
    if (drawing)
        m_drawing = drawing;
    if (!m_drawing && m_appContext)
        m_drawing = m_appContext->activeDrawWindow();
    if (!m_drawing || !m_drawing->scene()) {
        m_status->setText(tr("열린 도면이 없습니다."));
        m_model = Model3D();
        m_view->setModel(m_model, resetCamera);
        return;
    }

    setWindowTitle((m_mode == Mechanical ? tr("3D 기계용 - %1") : tr("3D 건축용 - %1"))
                       .arg(m_drawing->windowTitle()));

    if (m_mode == Mechanical) {
        const QSignalBlocker block(m_precision);   // MCP 가 정밀도를 바꿨을 수 있다
        const int idx = m_precision->findData(MechModel3D::radialSteps());
        if (idx >= 0)
            m_precision->setCurrentIndex(idx);
        m_fullModel = MechModel3D::build(m_drawing->scene());
        rebuildPartList();
        applyPartFilter(resetCamera);
        return;
    }

    const Model3D model = BuildingModel3D::build(m_drawing->scene(), m_drawing->layerManager());
    m_model = model;
    m_view->setModel(model, resetCamera);
    if (model.layers.isEmpty()) {
        m_status->setText(tr("높이가 설정된 레이어가 없습니다. 3D 건축용 > 레이어 높이 설정에서 벽 레이어 높이를 입력하세요."));
        return;
    }
    double volume = 0.0;
    int walls = 0;
    for (int i = 0; i < model.solids.size(); ++i) {
        volume += model.solids.at(i).volume();
        if (model.solids.at(i).kind == Model3DSolid::Wall)
            ++walls;
    }
    QString text = tr("벽 %1개 · 체적 %2 m³ · 문·창 %3개")
                       .arg(walls)
                       .arg(volume / 1e9, 0, 'f', 2)
                       .arg(model.openings.size());
    if (!model.warnings.isEmpty())
        text += tr(" · 경고 %1건 (3D 모델 검사에서 확인)").arg(model.warnings.size());
    m_status->setText(text);
}

void Model3DWindow::onRefresh() { showDrawing(nullptr, false); }

void Model3DWindow::setMode(Mode mode)
{
    m_mode = mode;
    m_precisionLabel->setVisible(mode == Mechanical);
    m_precision->setVisible(mode == Mechanical);
    m_partPanel->setVisible(mode == Mechanical);
    m_drawIso->setVisible(mode == Mechanical);
}

// ─────────────────────────────────────────────────────────────────────────────
// 기계용 부품 고르기
// ─────────────────────────────────────────────────────────────────────────────
namespace {
const int kPartIdRole = Qt::UserRole;
}

void Model3DWindow::rebuildPartList()
{
    const QSignalBlocker block(m_partList);
    m_partList->clear();
    const QList<MechModel3D::PartEntry> parts = MechModel3D::partList(m_fullModel);
    QSet<QString> present;
    for (int i = 0; i < parts.size(); ++i) {
        QListWidgetItem *it = new QListWidgetItem(parts.at(i).name, m_partList);
        it->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        it->setData(kPartIdRole, parts.at(i).id);
        it->setCheckState(m_hidden.contains(parts.at(i).id) ? Qt::Unchecked : Qt::Checked);
        present.insert(parts.at(i).id);
    }
    m_hidden.intersect(present);   // 지운 부품은 잊는다
}

void Model3DWindow::applyPartFilter(bool resetCamera)
{
    m_model = MechModel3D::filtered(m_fullModel, m_hidden);
    m_view->setModel(m_model, resetCamera);
    const int total = m_partList->count();
    QString text = MechModel3D::summaryText(m_model);
    if (total > 0)
        text = tr("부품 %1/%2 표시 · ").arg(total - m_hidden.size()).arg(total) + text;
    m_status->setText(text);
}

void Model3DWindow::onPartItemChanged(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString id = item->data(kPartIdRole).toString();
    if (item->checkState() == Qt::Checked)
        m_hidden.remove(id);
    else
        m_hidden.insert(id);
    applyPartFilter(false);
}

void Model3DWindow::showOnlyParts(const QSet<QString> &ids)
{
    m_hidden.clear();
    if (!ids.isEmpty()) {
        const QList<MechModel3D::PartEntry> parts = MechModel3D::partList(m_fullModel);
        for (int i = 0; i < parts.size(); ++i)
            if (!ids.contains(parts.at(i).id))
                m_hidden.insert(parts.at(i).id);
    }
    rebuildPartList();
    applyPartFilter(false);
}

void Model3DWindow::showOnlyPartNames(const QStringList &names, QStringList *unknown)
{
    const QList<MechModel3D::PartEntry> parts = MechModel3D::partList(m_fullModel);
    QSet<QString> ids;
    for (int n = 0; n < names.size(); ++n) {
        bool found = false;
        for (int i = 0; i < parts.size(); ++i)
            if (parts.at(i).name == names.at(n)) {
                ids.insert(parts.at(i).id);
                found = true;
            }
        if (!found && unknown)
            unknown->append(names.at(n));
    }
    if (ids.isEmpty() && !names.isEmpty()) {
        // 아는 이름이 하나도 없으면 아무것도 안 보이게 (전체로 되돌리지 않음)
        for (int i = 0; i < parts.size(); ++i)
            m_hidden.insert(parts.at(i).id);
        rebuildPartList();
        applyPartFilter(false);
        return;
    }
    showOnlyParts(ids);
}

QStringList Model3DWindow::visiblePartNames() const
{
    QStringList out;
    const QList<MechModel3D::PartEntry> parts = MechModel3D::partList(m_fullModel);
    for (int i = 0; i < parts.size(); ++i)
        if (!m_hidden.contains(parts.at(i).id))
            out << parts.at(i).name;
    return out;
}

void Model3DWindow::onPartsAll()
{
    showOnlyParts(QSet<QString>());
}

void Model3DWindow::onPartsNone()
{
    const QList<MechModel3D::PartEntry> parts = MechModel3D::partList(m_fullModel);
    for (int i = 0; i < parts.size(); ++i)
        m_hidden.insert(parts.at(i).id);
    rebuildPartList();
    applyPartFilter(false);
}
void Model3DWindow::onIso()     { m_view->setPreset(Model3DView::Iso); }
void Model3DWindow::onTop()     { m_view->setPreset(Model3DView::Top); }
void Model3DWindow::onFront()   { m_view->setPreset(Model3DView::Front); }
void Model3DWindow::onSide()    { m_view->setPreset(Model3DView::Side); }

QString Model3DWindow::drawingBaseName() const
{
    QString name = m_drawing ? m_drawing->windowTitle() : QString();
    name.remove(QStringLiteral("[*]"));
    name = QFileInfo(name.trimmed()).completeBaseName();
    return name.isEmpty() ? QStringLiteral("model") : name;
}

void Model3DWindow::onExportObj()
{
    if (m_model.solids.isEmpty() && m_model.meshes.isEmpty()) {
        QMessageBox::information(this, windowTitle(),
                                 m_mode == Mechanical
                                     ? tr("내보낼 기계 부품이 없습니다. 기계 Tools(축·깊은홈볼베어링·육각볼트)로 배치한 부품만 3D 가 됩니다.")
                                     : tr("내보낼 벽이 없습니다. 벽 레이어 높이를 먼저 설정하세요."));
        return;
    }
    const QString start = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation) +
                          QLatin1Char('/') + drawingBaseName() + QStringLiteral(".obj");
    const QString path = QFileDialog::getSaveFileName(this, tr("OBJ 내보내기"), start, tr("Wavefront OBJ (*.obj)"));
    if (path.isEmpty())
        return;
    QString err;
    if (!Model3DExport::writeObj(m_model, path, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    m_status->setText(tr("저장했습니다: %1 (+ .mtl)").arg(QDir::toNativeSeparators(path)));
}

void Model3DWindow::onOpenBlender()
{
    if (m_model.solids.isEmpty() && m_model.meshes.isEmpty()) {
        QMessageBox::information(this, windowTitle(),
                                 m_mode == Mechanical
                                     ? tr("내보낼 기계 부품이 없습니다. 기계 Tools(축·깊은홈볼베어링·육각볼트)로 배치한 부품만 3D 가 됩니다.")
                                     : tr("내보낼 벽이 없습니다. 벽 레이어 높이를 먼저 설정하세요."));
        return;
    }
    QString exe = Model3DExport::findBlenderExe();
    if (exe.isEmpty()) {
        exe = QFileDialog::getOpenFileName(this, tr("blender.exe 위치를 알려 주세요"),
                                           QStringLiteral("C:/Program Files"), tr("Blender (blender.exe)"));
        if (exe.isEmpty())
            return;
        Model3DExport::setBlenderExe(exe);
    }
    const QString obj = Model3DExport::tempObjPath(drawingBaseName());
    QString err;
    if (!Model3DExport::writeObj(m_model, obj, &err) || !Model3DExport::openInBlender(obj, exe, &err)) {
        QMessageBox::warning(this, windowTitle(), err);
        return;
    }
    m_status->setText(tr("Blender 를 여는 중입니다... (%1)").arg(QDir::toNativeSeparators(obj)));
}
