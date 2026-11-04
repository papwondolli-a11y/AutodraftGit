#include "holesectiontool.h"
#include "holedata.h"
#include "holeitem.h"
#include "holesectiongenerator.h"
#include "holecalloutformatter.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "drawingsettingscommand.h"
#include "entityfactory.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "snapmanager.h"
#include "tooldefaults.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeyEvent>
#include <QLabel>
#include <QPainterPath>
#include <QPixmap>
#include <QUndoStack>
#include <QUuid>
#include <QVBoxLayout>
#include <cmath>

// 마지막으로 고른 숨은선 레이어 (QSettings 영속)
QString HoleSectionTool::s_hiddenLineLayer = QString();

HoleSectionTool::HoleSectionTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitSelectHole), m_materialThickness(0.0), m_twoPointMode(false) {
    // 생성 시점엔 씬이 없으므로 전역/코드 기본값만. 도면값은 activate()에서 반영.
    s_hiddenLineLayer =
        ToolDefaults("HoleSectionTool", nullptr).getString("HiddenLineLayer", s_hiddenLineLayer);
}

void HoleSectionTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    // [3-2] 현재 도면의 숨은선 레이어 기본값 반영(도면값 없으면 전역/코드 기본값).
    s_hiddenLineLayer =
        ToolDefaults("HoleSectionTool", scene).getString("HiddenLineLayer", s_hiddenLineLayer);

    m_state = WaitSelectHole;

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("HOLESECTION (구멍 단면)"), CommandDock::Command);
    }
    showSelectPrompt();
}

void HoleSectionTool::deactivate() {
    if (m_scene) m_scene->clearOverlay();
    AbstractTool::deactivate();
}

void HoleSectionTool::showSelectPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(tr(" 단면으로 표현할 구멍을 선택 하세요 : "), {});
    }
}

void HoleSectionTool::showPlacePrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(tr(" 단면 형상의 상단면 기준점을 지정 하세요 : "), {});
    }
}

void HoleSectionTool::showMidPointPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(tr(" 다음점을 지정 하세요 : "), {});
    }
}

void HoleSectionTool::showDirectionPrompt() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->showInteractivePrompt(tr(" 방향을 설정 하세요 : "), {});
    }
}

void HoleSectionTool::cancelTool() {
    m_state = WaitSelectHole;
    if (m_scene) m_scene->clearOverlay();
    emit toolFinished();
}

void HoleSectionTool::keyPress(QKeyEvent *event) {
    if (event->key() == Qt::Key_Escape) {
        cancelTool();
        return;
    }
    AbstractTool::keyPress(event);
}

// HoleTool이 구멍을 생성할 때 쓰는 것과 동일한 규칙: 지정된 레이어가 비어있으면
// 현재 레이어로 대체. 중심선은 전역 중심선 레이어(DrawingSettingsCommand::s_centerLineLayer)를 우선 사용.
// 단, 정보 다이얼로그에서 "숨은선"을 켰으면 중심선을 제외한 나머지 선(기본/탭/카운터보어/
// 카운터싱크)만 지정한 레이어 하나로 통일한다 — 중심선은 숨은선 여부와 무관하게 항상
// 도면틀 설정에서 지정한 중심선 레이어(아래 Center 분기)를 따른다.
QString HoleSectionTool::layerNameForRole(HoleSectionLineRole role) const {
    if (m_hiddenLineMode && !m_hiddenLineLayer.isEmpty() && role != HoleSectionLineRole::Center) {
        return m_hiddenLineLayer;
    }

    switch (role) {
    case HoleSectionLineRole::Base:
        return !m_selectedHole.baseLayer.isEmpty() ? m_selectedHole.baseLayer : currentLayerName();
    case HoleSectionLineRole::Thread:
        return !m_selectedHole.threadLayer.isEmpty() ? m_selectedHole.threadLayer : currentLayerName();
    case HoleSectionLineRole::Counterbore:
        return !m_selectedHole.cbLayer.isEmpty() ? m_selectedHole.cbLayer : currentLayerName();
    case HoleSectionLineRole::Countersink:
        return !m_selectedHole.csLayer.isEmpty() ? m_selectedHole.csLayer : currentLayerName();
    case HoleSectionLineRole::Center:
        return !DrawingSettingsCommand::s_centerLineLayer.isEmpty()
                   ? DrawingSettingsCommand::s_centerLineLayer
                   : currentLayerName();
    }
    return currentLayerName();
}

// 지정 레이어의 색상/두께 + 선종류(점선/일점쇄선 등)를 대시 패턴으로 반영한 펜을 만든다.
//
// 대시 패턴은 DXF 임포트(DxfManager::applyLayerStyle)·고스트 미리보기와 완전히 동일하게
// 씬의 레이어 스타일 캐시(MyGraphicsScene::getLayerStyle)에서 가져온다. 이 캐시는
// DrawSubWindow::onLayerPropertyChanged 를 통해 항상 최신 상태로 유지되며, 대소문자
// 차이까지 흡수한다. AbstractTool::getLayerPen() 처럼 linetype 이름으로 매번 재조회하면
// 레이어명 불일치·미등록 linetype 등으로 패턴이 비어 실선으로 떨어지던 문제가 있었다.
// 캐시에 아직 없을 때만(신규 도면 등) linetype 이름으로 직접 조회하는 경로를 남겨둔다.
QPen HoleSectionTool::sectionPenForLayer(const QString &layerName) const {
    QPen pen(Qt::black, 0);
    pen.setCosmetic(true);

    IAppContext *ctx = getContext();

    // 색상/두께는 레이어 매니저를 우선 신뢰(대소문자만 달라도 매칭).
    Layer *layer = nullptr;
    if (ctx && ctx->layerManager()) {
        LayerManager *lm = ctx->layerManager();
        layer = lm->getLayer(layerName);
        if (!layer && !layerName.isEmpty()) {
            const QList<Layer> all = lm->getAllLayers();
            for (int i = 0; i < all.size(); ++i) {
                if (all.at(i).name().compare(layerName, Qt::CaseInsensitive) == 0) {
                    layer = lm->getLayer(all.at(i).name());
                    break;
                }
            }
        }
        if (!layer) layer = lm->getCurrentLayer();
    }
    if (layer) {
        pen = QPen(layer->color());
        pen.setWidthF(layer->weight());
    }

    // 대시 패턴: 1순위 = 씬 레이어 스타일 캐시(검증된 경로), 2순위 = linetype 이름 직접 조회.
    QVector<qreal> dashPattern;
    if (m_scene && !layerName.isEmpty()) {
        dashPattern = m_scene->getLayerStyle(layerName).dashPattern;
    }
    if (dashPattern.isEmpty() && layer && ctx && ctx->lineTypeManager()) {
        const QString lt = layer->linetype();
        if (lt != "Continuous" && lt != "Continous") {
            dashPattern = ctx->lineTypeManager()->getQtDashPattern(lt);
        }
    }
    // Qt는 짝수 길이 패턴만 그린다. linetype.lin 의 일부 항목("Continous", "DashLine(large)"
    // 등)은 홀수 개로 파싱되므로, 그럴 땐 마지막 값을 한 번 더 붙여(dash=gap) 짝수로 맞춘다.
    if (dashPattern.size() % 2 != 0) {
        dashPattern.append(dashPattern.last());
    }

    if (!dashPattern.isEmpty()) {
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern(dashPattern);
    } else {
        pen.setStyle(Qt::SolidLine);
    }

    pen.setCosmetic(true);
    return pen;
}

void HoleSectionTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;

    if (button == Qt::RightButton) {
        cancelTool();
        return;
    }
    if (button != Qt::LeftButton) return;

    if (m_state == WaitSelectHole) {
        QGraphicsItem *item = m_scene->itemAt(pos, QTransform());
        HoleItem *hole = dynamic_cast<HoleItem *>(item);
        if (!hole) {
            const QList<QGraphicsItem *> items = m_scene->items(QRectF(pos.x() - 5, pos.y() - 5, 10, 10));
            for (int i = 0; i < items.size(); ++i) {
                if ((hole = dynamic_cast<HoleItem *>(items[i]))) break;
            }
        }
        if (!hole) return;

        m_selectedHole = hole->holeData();
        m_materialThickness = 0.0;
        m_twoPointMode = false;

        // [추가] 속성 dock에서 이 구멍이 수정될 때 단면을 찾아 매칭할 수 있도록,
        // 아직 그룹 ID가 없는(이 기능 이전에 만들어진) 구멍이면 여기서 새로 발급해 태깅한다.
        m_selectedHoleGroupId = hole->data(HOLE_DATA_GROUP_ID).toString();
        if (m_selectedHoleGroupId.isEmpty()) {
            m_selectedHoleGroupId = QUuid::createUuid().toString();
            hole->setData(HOLE_DATA_GROUP_ID, m_selectedHoleGroupId);
        }

        QDialog infoDlg(nullptr);
        infoDlg.setWindowTitle(QObject::tr("구멍 정보"));
        QVBoxLayout *layout = new QVBoxLayout(&infoDlg);

        QLabel *infoLabel = new QLabel(HoleCalloutFormatter::formatSingle(m_selectedHole), &infoDlg);
        layout->addWidget(infoLabel);

        QDoubleSpinBox *valueSpin = new QDoubleSpinBox(&infoDlg);
        valueSpin->setRange(0.0, 10000.0);
        valueSpin->setDecimals(2);
        if (m_selectedHole.throughHole) {
            layout->addWidget(new QLabel(QObject::tr("관통 구멍 표시용 재료 두께 :"), &infoDlg));
            valueSpin->setValue(m_selectedHole.diameter);
        } else {
            layout->addWidget(new QLabel(QObject::tr("탭 깊이 :"), &infoDlg));
            valueSpin->setValue(m_selectedHole.threadDepth);
        }

        QCheckBox *twoPointCheck = new QCheckBox(QObject::tr("2점 지정"), &infoDlg);
        twoPointCheck->setChecked(false);
        QObject::connect(twoPointCheck, &QCheckBox::toggled, valueSpin, &QDoubleSpinBox::setDisabled);

        QHBoxLayout *valueRow = new QHBoxLayout();
        valueRow->addWidget(valueSpin);
        valueRow->addWidget(twoPointCheck);
        layout->addLayout(valueRow);

        // [추가] 숨은선 표시 — 켜면 이 단면의 모든 선(기본/탭/카운터보어/카운터싱크/중심선)을
        // 역할별 기본 레이어 대신 선택한 레이어 하나로 통일해서 그린다. 초기값은 비활성화.
        IAppContext *ctx = getContext();
        QCheckBox *hiddenLineCheck = new QCheckBox(QObject::tr("숨은선"), &infoDlg);
        hiddenLineCheck->setChecked(false);

        QComboBox *hiddenLineCombo = new QComboBox(&infoDlg);
        if (ctx && ctx->layerManager()) {
            for (const Layer &layer : ctx->layerManager()->getAllLayers()) {
                QPixmap px(16, 16);
                px.fill(layer.color());
                hiddenLineCombo->addItem(QIcon(px), layer.name(), layer.name());
            }
        }
        // 콤보를 첫 항목("0" 등 실선 레이어)로 두면 사용자가 매번 숨은선 레이어를 다시
        // 골라야 한다. 지난번 선택(s_hiddenLineLayer), 없으면 현재 레이어로 미리 맞춰준다.
        {
            int idx = -1;
            if (!s_hiddenLineLayer.isEmpty())
                idx = hiddenLineCombo->findData(s_hiddenLineLayer);
            if (idx < 0)
                idx = hiddenLineCombo->findData(currentLayerName());
            if (idx >= 0)
                hiddenLineCombo->setCurrentIndex(idx);
        }
        hiddenLineCombo->setEnabled(false);
        QObject::connect(hiddenLineCheck, &QCheckBox::toggled, hiddenLineCombo, &QComboBox::setEnabled);

        QHBoxLayout *hiddenLineRow = new QHBoxLayout();
        hiddenLineRow->addWidget(hiddenLineCheck);
        hiddenLineRow->addWidget(new QLabel(QObject::tr("레이어 :"), &infoDlg));
        hiddenLineRow->addWidget(hiddenLineCombo);
        layout->addLayout(hiddenLineRow);

        QDialogButtonBox *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &infoDlg);
        layout->addWidget(buttons);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &infoDlg, &QDialog::accept);
        QObject::connect(buttons, &QDialogButtonBox::rejected, &infoDlg, &QDialog::reject);

        if (infoDlg.exec() != QDialog::Accepted) return; // 취소 시 선택 취소, 계속 WaitSelectHole 유지

        m_twoPointMode = twoPointCheck->isChecked();
        if (m_selectedHole.throughHole) {
            m_materialThickness = valueSpin->value();
        } else {
            m_selectedHole.threadDepth = valueSpin->value();
        }

        m_hiddenLineMode = hiddenLineCheck->isChecked();
        m_hiddenLineLayer = hiddenLineCombo->currentData().toString();
        if (m_hiddenLineMode && !m_hiddenLineLayer.isEmpty()) {
            s_hiddenLineLayer = m_hiddenLineLayer; // 다음 실행을 위해 기억
            ToolDefaults("HoleSectionTool", m_scene)
                .put("HiddenLineLayer", s_hiddenLineLayer);
        }

        m_state = WaitPlacePoint;
        showPlacePrompt();
        return;
    }

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    if (ctx && ctx->snapManager()) {
        // 방향 설정 단계: 객체 스냅(osnap)이 우선 적용되면 직교(ortho)가 무시되는 문제가 있어
        // (SnapManager::snapPoint가 osnap을 찾으면 ortho 계산 전에 바로 반환) 이 단계에서는
        // scene을 넘기지 않아 osnap을 끄고 그리드/직교만 적용되도록 함.
        bool isDirectionStep = (m_state == WaitSetDirection);
        bool isMidPointStep = (m_state == WaitPlaceMidPoint);
        QPointF orthoBase = isDirectionStep ? m_topCenter : (isMidPointStep ? m_midPointFirst : QPointF());
        // p2(중간점) 단계는 osnap으로 두 점을 정확히 집는 게 목적이므로 osnap을 켜 둔다.
        // snapPoint()는 osnap이 잡히면 ortho보다 우선 반환하므로(osnap 성공 시 ortho 미적용,
        // 실패 시에만 ortho 적용) 이 조합으로도 orthoBase에 의한 직교 폴백은 그대로 동작한다.
        MyGraphicsScene *snapScene = isDirectionStep ? nullptr : m_scene;
        snappedPos = ctx->snapManager()->snapPoint(pos, snapScene, orthoBase).snappedPos;
    }

    if (m_state == WaitPlacePoint) {
        // Shift+좌클릭(p1): 두 점을 지정해 그 중간점을 기준점으로 사용
        if (QApplication::keyboardModifiers() & Qt::ShiftModifier) {
            m_midPointFirst = snappedPos;
            m_state = WaitPlaceMidPoint;
            showMidPointPrompt();
            return;
        }

        m_topCenter = snappedPos;
        m_state = WaitSetDirection;
        showDirectionPrompt();
        return;
    }

    if (m_state == WaitPlaceMidPoint) {
        // p2 확정: p1-p2의 중간점을 상단면 기준점으로 사용
        m_topCenter = (m_midPointFirst + snappedPos) / 2.0;
        m_state = WaitSetDirection;
        showDirectionPrompt();
        return;
    }

    // WaitSetDirection: 기준점 -> 클릭 위치로 향하는 방향을 단면의 깊이 방향으로 사용
    QPointF direction = snappedPos - m_topCenter;
    applyTwoPointOverride(direction);
    if (std::hypot(direction.x(), direction.y()) < 1e-6) direction = QPointF(0.0, 1.0);
    placeSection(direction);
}

// 2점 지정 모드: 기준점(m_topCenter)과 방향설정 점 사이의 거리로 수동 입력값을 덮어씀
// (관통 구멍이면 재료두께, 막힌 구멍이면 탭깊이).
void HoleSectionTool::applyTwoPointOverride(const QPointF &direction) {
    if (!m_twoPointMode) return;
    double dist = std::hypot(direction.x(), direction.y());
    if (m_selectedHole.throughHole) {
        m_materialThickness = dist;
    } else {
        // buildLines()에서 나사산 깊이는 항상 hole.depth 이하로 clamp되므로(threadBottomV =
        // min(curTopV + threadDepth, bottomV)), depth를 그대로 두면 2점 거리가 원래 구멍 깊이보다
        // 클 때 화면에 아무 변화가 없어 "적용 안 됨"으로 보임. depth도 함께 덮어써 clamp 상한을
        // 같이 늘려야 탭깊이 2점 지정이 실제로 반영됨.
        m_selectedHole.depth = dist;
        m_selectedHole.threadDepth = dist;
    }
}

void HoleSectionTool::placeSection(const QPointF &direction) {
    IAppContext *ctx = getContext();
    QList<HoleSectionLine> lines =
        HoleSectionGenerator::buildLines(m_selectedHole, m_topCenter, m_materialThickness, direction);

    if (!lines.isEmpty() && ctx && ctx->entityFactory()) {
        if (ctx->undoStack()) ctx->undoStack()->beginMacro(QObject::tr("Add Hole Section"));

        for (int i = 0; i < lines.size(); ++i) {
            const HoleSectionLine &sectionLine = lines.at(i);
            QString layerName = layerNameForRole(sectionLine.role);
            QPen pen = sectionPenForLayer(layerName);
            QGraphicsItem *layerRoot = nullptr;
            if (ctx->layerManager()) {
                Layer *targetLayer = ctx->layerManager()->getLayer(layerName);
                if (targetLayer && m_scene) layerRoot = m_scene->getLayerRoot(targetLayer->name());
            }

            QGraphicsItem *lineItem = ctx->entityFactory()->createLine(sectionLine.line, pen);
            if (!lineItem) continue;
            if (CadItem *ci = dynamic_cast<CadItem *>(lineItem)) {
                ci->setLayerName(layerName);
                ci->setPen(pen); // 레이어명 지정 후에도 선종류(대시 패턴)가 유지되도록 다시 적용
            }

            // [추가] 원본 구멍과 배치 정보(재생성에 필요)를 함께 태깅.
            // 속성 dock에서 구멍이 수정되면 이 태그로 단면을 찾아 같은 위치/방향으로 다시 그린다.
            if (!m_selectedHoleGroupId.isEmpty()) {
                lineItem->setData(HOLE_SECTION_GROUP_ID, m_selectedHoleGroupId);
                lineItem->setData(HOLE_SECTION_TOP_X, m_topCenter.x());
                lineItem->setData(HOLE_SECTION_TOP_Y, m_topCenter.y());
                lineItem->setData(HOLE_SECTION_DIR_X, direction.x());
                lineItem->setData(HOLE_SECTION_DIR_Y, direction.y());
                lineItem->setData(HOLE_SECTION_MATERIAL_THICKNESS, m_materialThickness);
            }

            if (layerRoot) lineItem->setParentItem(layerRoot);
            if (lineItem->scene() == m_scene) m_scene->removeItem(lineItem);

            if (ctx->undoStack()) {
                ctx->undoStack()->push(new AddEntityCommand(m_scene, lineItem));
            } else if (m_scene) {
                m_scene->addItem(lineItem);
            }
        }

        if (ctx->undoStack()) ctx->undoStack()->endMacro();
    }

    if (m_scene) m_scene->clearOverlay();
    m_state = WaitSelectHole;
    showSelectPrompt();
}

void HoleSectionTool::mouseMove(QPointF pos) {
    if (!m_scene) return;
    m_currentMousePos = pos;

    IAppContext *ctx = getContext();
    QPointF snappedPos = pos;
    SnapType snapType = SnapType::None;
    if (ctx && ctx->snapManager()) {
        // 방향 설정 단계: osnap이 ortho보다 먼저 반환되어 버리는 문제를 피하기 위해 이
        // 단계에서는 scene을 넘기지 않아 osnap을 끄고 그리드/직교만 적용되도록 함.
        bool isDirectionStep = (m_state == WaitSetDirection);
        bool isMidPointStep = (m_state == WaitPlaceMidPoint);
        QPointF orthoBase = isDirectionStep ? m_topCenter : (isMidPointStep ? m_midPointFirst : QPointF());
        // p2(중간점) 단계는 osnap으로 두 점을 정확히 집는 게 목적이므로 osnap을 켜 둔다.
        MyGraphicsScene *snapScene = isDirectionStep ? nullptr : m_scene;
        SnapManager::SnapResult res = ctx->snapManager()->snapPoint(pos, snapScene, orthoBase);
        snappedPos = res.snappedPos;
        snapType = res.type;
    }
    m_scene->setSnapMarker(snappedPos, snapType);

    if (m_state == WaitPlaceMidPoint) {
        // p1 -> 현재 마우스 위치를 잇는 안내선만 미리보기로 표시 (중간점은 p2 확정 시 계산)
        QPainterPath path;
        path.moveTo(m_midPointFirst);
        path.lineTo(snappedPos);
        updateGhostPreview(path, currentLayerName());
        return;
    }

    QPointF topCenter;
    QPointF direction(0.0, 1.0);
    if (m_state == WaitPlacePoint) {
        topCenter = snappedPos;
    } else if (m_state == WaitSetDirection) {
        topCenter = m_topCenter;
        QPointF d = snappedPos - m_topCenter;
        applyTwoPointOverride(d);
        if (std::hypot(d.x(), d.y()) >= 1e-6) direction = d;
    } else {
        return;
    }

    QList<HoleSectionLine> lines =
        HoleSectionGenerator::buildLines(m_selectedHole, topCenter, m_materialThickness, direction);
    QPainterPath path;
    for (int i = 0; i < lines.size(); ++i) {
        const HoleSectionLine &sectionLine = lines.at(i);
        path.moveTo(sectionLine.line.p1());
        path.lineTo(sectionLine.line.p2());
    }
    // 숨은선 모드면 미리보기도 숨은선 레이어 스타일(점선 등)로 보여 결과와 일치시킨다.
    const QString previewLayer = (m_hiddenLineMode && !m_hiddenLineLayer.isEmpty())
                                     ? m_hiddenLineLayer
                                     : currentLayerName();
    updateGhostPreview(path, previewLayer);
}
