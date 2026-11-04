#include "propertydock.h"
#include "cadcommands.h"
#include "boxtextutil.h"
#include "textitem.h"
#include <QPushButton>
#include <QtMath>
#include "caditem.h"
#include "drawingsettingscommand.h"
#include "entityfactory.h"
//#include "holedata.h"
//#include "holesectiongenerator.h"
#include "holesync.h"
#include "holecalloutlink.h"
#include "leaderitem.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "myhole.h"
//#include "lineitem.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "planarchdata.h"
#include "plandoortool.h"
#include "spacefinish.h"
#include "plandoorsimpletool.h"
#include "plandoordoubletool.h"
#include "plandoorbiswingtool.h"
#include "plandoordoublebiswingtool.h"
#include "planrevolvingdoortool.h"
#include "planwindowtool.h"
#include "wallpositiondialog.h"
#include "plandoorsetupdialog.h"
#include "selectionmanager.h"
#include "drawsubwindow.h"
#include <QButtonGroup>
#include <QComboBox>
#include <QInputDialog>
#include <QToolButton>
#include <QDoubleSpinBox>
#include <QIcon>
#include <QPixmap>
#include <QCheckBox>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QDebug>
#include <QHeaderView>
#include <QMessageBox>
#include <QUndoStack>
#include <QVBoxLayout>
#include <QTimer> // [추가] UI 실시간 갱신용 타이머

PropertyDock::PropertyDock(IAppContext *appContext, QWidget *parent)
    : QDockWidget(tr("속성 목록"), parent), m_appContext(appContext) {
    this -> setObjectName("PropertyDock");
    setAttribute(Qt::WA_StyledBackground, true);
    QWidget *container = new QWidget(this);
    container -> setObjectName("PropertyTableDock");
    QVBoxLayout *layout = new QVBoxLayout(container);
    layout->setContentsMargins(3,3, 3, 3);
    layout->setSpacing(2);

    m_topTable = new QTableWidget(0, 2, this);
    m_topTable->setObjectName("PropertyTableWidgetTop");
    m_topTable->setHorizontalHeaderLabels({tr("속성"), tr("값")});
    m_topTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_topTable->verticalHeader()->setVisible(false);

    m_bottomTable = new QTableWidget(0, 2, this);
    m_bottomTable->setObjectName("PropertyTableWidgetBottom");
    m_bottomTable->horizontalHeader()->setVisible(false);
    m_bottomTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_bottomTable->verticalHeader()->setVisible(false);

    m_extTable = new QTableWidget(0, 2, this);
    m_extTable->setObjectName("PropertyTableWidgetExt");
    m_extTable->horizontalHeader()->setVisible(false);
    m_extTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_extTable->verticalHeader()->setVisible(false);

    m_extTable2 = new QTableWidget(0, 2, this);
    m_extTable2->setObjectName("PropertyTableWidgetExt2");
    m_extTable2->horizontalHeader()->setVisible(false);
    m_extTable2->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_extTable2->verticalHeader()->setVisible(false);

    layout->addWidget(m_topTable);
    layout->addWidget(m_bottomTable);
    layout->addWidget(m_extTable);
    layout->addWidget(m_extTable2);

    m_deleteSymbolBtn = new QPushButton(tr("삭제"), container);
    m_deleteSymbolBtn->setObjectName("DeleteSymbolButton");
    m_deleteSymbolBtn->setVisible(false);
    layout->addWidget(m_deleteSymbolBtn);
    connect(m_deleteSymbolBtn, SIGNAL(clicked()), this, SLOT(onDeleteSymbol()));

    setWidget(container);

    if (m_appContext && m_appContext->selectionManager()) {
        setSelectionManager(m_appContext->selectionManager());
    }

    connect(m_topTable, &QTableWidget::cellChanged, this, &PropertyDock::onPropertyChanged);
    connect(m_bottomTable, &QTableWidget::cellChanged, this, &PropertyDock::onPropertyChanged);
    connect(m_extTable, &QTableWidget::cellChanged, this, &PropertyDock::onPropertyChanged);
    connect(m_extTable2, &QTableWidget::cellChanged, this, &PropertyDock::onPropertyChanged);
}

void PropertyDock::onSelectionChanged(const QList<QGraphicsItem *> &items) {
    m_selectedItems.clear();
    for (int i = 0; i < items.size(); ++i) {
        if (items[i])
            m_selectedItems.append(items[i]);
    }
    updateDisplay();

    if (!items.isEmpty() && m_appContext) {
        DrawSubWindow *dw = m_appContext->activeDrawWindow();
        if (dw && (dw->tool() == dw->defaultTool())) {
            if (!this->isVisible()) {
                this->show();
            }
            this->raise();
        }
    }
}

void PropertyDock::setSelectionManager(SelectionManager *mgr) {
    if (m_currentManager) {
        disconnect(m_currentManager.data(), &SelectionManager::selectionChanged,
                   this, &PropertyDock::onSelectionChanged);
    }

    m_currentManager = mgr;

    if (m_currentManager) {
        connect(m_currentManager.data(), &SelectionManager::selectionChanged, this,
                &PropertyDock::onSelectionChanged);
        onSelectionChanged(m_currentManager->selectedItems());
    } else {
        onSelectionChanged({});
    }
}

void PropertyDock::updateDisplay() {
    m_isUpdating = true;
    clearTables();

    if (m_selectedItems.isEmpty()) {
        m_bottomTable->setVisible(false);
        m_extTable2->setVisible(false);
        m_isUpdating = false;
        return;
    }

    CadItem *first = dynamic_cast<CadItem *>(m_selectedItems.first());
    if (!first) {
        m_bottomTable->setVisible(false);
        m_extTable2->setVisible(false);
        m_isUpdating = false;
        return;
    }

    // ── 심볼 레지스트리 감지 (PlanDoor · PlanWindow) ──────────────────────────
    QString groupId = first->data(PD_DATA_GROUP_ID).toString();
    if (!groupId.isEmpty()) {
        bool allSameGroup = true;
        for (int i = 1; i < m_selectedItems.size(); ++i) {
            if (m_selectedItems.at(i)->data(PD_DATA_GROUP_ID).toString() != groupId) {
                allSameGroup = false;
                break;
            }
        }
        if (allSameGroup) {
            MyGraphicsScene *scene = dynamic_cast<MyGraphicsScene *>(first->scene());
            if (scene && scene->symbolRegistry()->hasSymbol(groupId)) {
                m_currentGroupId = groupId;
                showSymbolProperties(scene->symbolRegistry()->symbolInfo(groupId));
                m_isUpdating = false;
                return;
            }
        }
    }
    // ─────────────────────────────────────────────────────────────────────────

    QMap<QString, QVariant> commonProps = first->getProperties();

    for (int i = 1; i < m_selectedItems.size(); ++i) {
        CadItem *item = dynamic_cast<CadItem *>(m_selectedItems.at(i));
        if (!item) continue;

        QMap<QString, QVariant> itemProps = item->getProperties();

        QList<QString> keys = commonProps.keys();
        for (int j = 0; j < keys.size(); ++j) {
            QString key = keys[j];
            if (!itemProps.contains(key)) {
                commonProps.remove(key);
            } else {
                if (commonProps[key] != itemProps[key]) {
                    commonProps[key] = QString("*Varies*");
                }
            }
        }
    }

    QString typeStr = commonProps.value("Type").toString();
    // 상세도 경계원: Type 은 "Circle" 그대로지만 IsDetailRegion 플래그로 별도 레이아웃
    bool isDetailRegion = commonProps.value("IsDetailRegion").toBool();
    bool isLine = (typeStr == "Line");
    bool isCircle = (typeStr == "Circle") && !isDetailRegion;
    bool isArc = (typeStr == "Arc");
    bool isDim = (typeStr == "Dimension" || typeStr == "BaseDimension");
    bool isText = (typeStr == "Text");
    bool isHole = (typeStr == "Hole");

    if (isLine || isCircle || isArc || isDim || isText || isHole || isDetailRegion) {
        QStringList orderTop;
        QStringList orderBottom;
        QStringList orderExt;
        QStringList orderExt2; // [신규] Hole 3번째 활성 그룹용
        QString bottomHeader = tr("공차");
        QString extHeader = tr("치수보조선");
        QString ext2Header;

        if (isLine) {
            orderTop = {"Type", "Layer", "Length", "StartPoint", "EndPoint"};
            if (commonProps.contains("MLWidth"))
                orderTop.append("MLWidth");
        } else if (isCircle) {
            orderTop = {"Type", "Layer", "Radius", "CenterPoint"};
        } else if (isDetailRegion) {
            // 상세도 경계원: 반지름/중심점 대신 축척만 노출 (편집 시 즉시 상세도에 반영)
            orderTop = {"Type", "Layer", "Scale"};
        } else if (isArc) {
            orderTop = {"Type", "Layer", "Radius", "CenterPoint", "StartAngle", "SpanAngle"};
        } else if (isText) {
            orderTop = {"Type", "Layer", "Content", "FontFamily", "FontSize", "Rotation"};
        } else if (isDim) {
            orderTop = {"Type", "Layer", "DimType", "Content", "Prefix", "Suffix", "Precision", "SuppressZero", "ThousandSep"};
            orderBottom = {"TolType", "TolPrecision", "SuppressTolZero", "TolUpper", "TolLower", "FitCode", "FitShowValues"};
            orderExt = {"HideExt1", "HideExt2"};
        } else if (isHole) {
            // [수정] 기본 구멍 정보만 위쪽 표에 남기고, 탭가공/카운터보어/카운터싱크는
            // 체크박스로 켜고 끄면서 활성화된 것만 치수 속성창처럼 별도 표로 분리해 보여준다.
            // BaseLayer가 사실상 "도면층" 역할을 대신하므로 "Layer"는 목록에 넣지 않는다
            // (기본레이어와 도면층이 같은 레이어 목록을 중복으로 보여주는 것 방지).
            bool throughHole = commonProps.value("ThroughHole").toBool();
            orderTop = {"Type", "BaseLayer", "CenterX", "CenterY", "Diameter", "ThroughHole"};
            if (!throughHole) orderTop.append("Depth"); // 관통이면 블라인드 깊이는 의미 없음
            orderTop.append("ToleranceClass");
            orderTop.append("SymbolStyle");
            orderTop.append("IsThreaded");
            orderTop.append("HasCounterbore");
            orderTop.append("HasCountersink");

            // 체크된(활성화된) 가공 방법만 순서대로 bottom → ext → ext2 슬롯에 채운다.
            int slot = 0;
            auto assignGroup = [&](bool active, const QString &header, const QStringList &groupKeys) {
                if (!active) return;
                if (slot == 0) { bottomHeader = header; orderBottom = groupKeys; }
                else if (slot == 1) { extHeader = header; orderExt = groupKeys; }
                else if (slot == 2) { ext2Header = header; orderExt2 = groupKeys; }
                ++slot;
            };
            assignGroup(commonProps.value("IsThreaded").toBool(), tr("탭가공"),
                        {"ThreadSpec", "ThreadClass", "ThreadDepth", "ThreadLayer"});
            assignGroup(commonProps.value("HasCounterbore").toBool(), tr("카운터보어 가공"),
                        {"CbDiameter", "CbDepth", "CbLayer"});
            assignGroup(commonProps.value("HasCountersink").toBool(), tr("카운터싱크 가공"),
                        {"CsDiameter", "CsAngle", "CsLayer"});
        }

        auto populateTable = [&](QTableWidget *table, const QStringList &order, int startRow = 0) {
            table->setRowCount(startRow + order.size());
            for (int i = 0; i < order.size(); ++i) {
                int row = startRow + i;
                QString key = order[i];
                QString displayKey = translateKey(key);
                QVariant displayValue;

                if (key == "StartPoint") {
                    QVariant sx = commonProps.value("StartX");
                    QVariant sy = commonProps.value("StartY");
                    if (sx.toString() == "*Varies*" || sy.toString() == "*Varies*") displayValue = "*Varies*";
                    else displayValue = QString("%1, %2").arg(formatValue(sx),formatValue(sy));
                } else if (key == "EndPoint") {
                    QVariant ex = commonProps.value("EndX");
                    QVariant ey = commonProps.value("EndY");
                    if (ex.toString() == "*Varies*" || ey.toString() == "*Varies*") displayValue = "*Varies*";
                    else displayValue = QString("%1, %2").arg(formatValue(ex),formatValue(ey));
                } else if (key == "CenterPoint") {
                    QVariant cx = commonProps.value("CenterX");
                    QVariant cy = commonProps.value("CenterY");
                    if (cx.toString() == "*Varies*" || cy.toString() == "*Varies*") displayValue = "*Varies*";
                    else displayValue = QString("%1, %2").arg(formatValue(cx),formatValue(cy));
                } else if (key == "Content" && isDim) {
                    // [수정] "Content"는 머리말/꼬리말이 이미 합쳐진 문자열이라 이 셀을 그대로
                    // 편집해서 커밋하면(onPropertyChanged) 다음 렌더링 때 머리말/꼬리말이 또
                    // 붙어 중복 표기된다(DimTextEditDialog에서 겪었던 것과 동일한 원인).
                    // 합쳐지기 전의 순수 본문값(RawValue)을 보여주고 편집하도록 한다.
                    displayValue = commonProps.value("RawValue");
                } else if (key == "Type" && isDetailRegion) {
                    // 실제 Type 은 "Circle" 이지만 사용자에겐 "상세영역"으로 표기
                    displayValue = tr("상세영역");
                } else {
                    displayValue = commonProps.value(key);
                }

                QTableWidgetItem *keyItem = new QTableWidgetItem(displayKey);
                keyItem->setData(Qt::UserRole, key);
                keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
                table->setItem(row, 0, keyItem);

                if (key == "Layer") {
                    QComboBox *combo = new QComboBox(table);
                    if (m_appContext && m_appContext->layerManager()) {
                        QList<Layer> layers = m_appContext->layerManager()->getAllLayers();
                        for (int j = 0; j < layers.size(); ++j) {
                            QPixmap px(16, 16);
                            px.fill(layers[j].color());
                            combo->addItem(QIcon(px), layers[j].name());
                        }
                    }
                    combo->setCurrentText(displayValue.toString());
                    table->setCellWidget(row, 1, combo);
                    connect(combo, &QComboBox::currentTextChanged, this, &PropertyDock::onLayerChanged);
                } else if (key == "Scale") {
                    // 상세영역 축척 — 0.5 단위 스핀박스 (편집 시 상세도 즉시 재스케일)
                    QDoubleSpinBox *dsb = new QDoubleSpinBox(table);
                    dsb->setRange(0.1, 1000.0);
                    dsb->setDecimals(2);
                    dsb->setSingleStep(0.5);
                    dsb->setValue(displayValue.toDouble());
                    dsb->setProperty("key", key);
                    table->setCellWidget(row, 1, dsb);
                    connect(dsb, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                            this, &PropertyDock::onDoublePropertyChanged);
                } else if (key == "Diameter" && isHole && displayValue.toString() != "*Varies*") {
                    // [추가] 구멍 기본(드릴)지름 - 스핀박스 화살표/휠로 1단위씩 증감하며
                    // 실시간으로 화면(및 탭가공 시 골지름선)에 반영되도록 한다. 연결된 구멍
                    // 단면 확인 다이얼로그는 클릭마다 뜨면 안 되므로 editingFinished에서만 띄운다.
                    QDoubleSpinBox *dsb = new QDoubleSpinBox(table);
                    dsb->setRange(0.1, 9999.0);
                    dsb->setDecimals(2);
                    dsb->setSingleStep(1.0);
                    dsb->setValue(displayValue.toDouble());
                    dsb->setProperty("key", key);
                    table->setCellWidget(row, 1, dsb);
                    connect(dsb, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                            this, &PropertyDock::onDoublePropertyChanged);
                    connect(dsb, &QDoubleSpinBox::editingFinished,
                            this, &PropertyDock::onHoleDiameterEditingFinished);
                } else if (key == "BaseLayer" || key == "ThreadLayer" || key == "CbLayer" || key == "CsLayer") {
                    // [추가] Hole의 레이어 오버라이드 4종 — HoleDialog의 "기본레이어" 콤보박스와
                    // 동일하게 색상 아이콘 + 레이어 이름을 함께 표기한다.
                    QComboBox *combo = new QComboBox(table);
                    combo->addItem(tr(" 현재 레이어 "), "");
                    if (m_appContext && m_appContext->layerManager()) {
                        QList<Layer> layerList = m_appContext->layerManager()->getAllLayers();
                        for (int j = 0; j < layerList.size(); ++j) {
                            QPixmap px(16, 16);
                            px.fill(layerList[j].color());
                            combo->addItem(QIcon(px), layerList[j].name(), layerList[j].name());
                        }
                    }
                    int idx = combo->findData(displayValue.toString());
                    combo->setCurrentIndex(idx >= 0 ? idx : 0);
                    combo->setProperty("key", key);
                    table->setCellWidget(row, 1, combo);
                    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                            this, &PropertyDock::onLayerNamePropertyChanged);
                } else if (key == "IsThreaded" || key == "HasCounterbore" || key == "HasCountersink") {
                    // [추가] 켜고 끄면 아래 그룹 표가 즉시 나타나거나 사라져야 하므로 실제
                    // 체크박스 위젯을 쓴다. 토글되면 onCheckBoxPropertyChanged에서 updateDisplay()로
                    // 다시 그려 활성화된 그룹 표만 보이도록 갱신한다.
                    QWidget *wrap = new QWidget(table);
                    QHBoxLayout *hl = new QHBoxLayout(wrap);
                    hl->setContentsMargins(0, 0, 0, 0);
                    hl->setAlignment(Qt::AlignCenter);
                    QCheckBox *checkbox = new QCheckBox(wrap);
                    checkbox->setChecked(displayValue.toBool());
                    checkbox->setProperty("key", key);
                    hl->addWidget(checkbox);
                    table->setCellWidget(row, 1, wrap);
                    connect(checkbox, &QCheckBox::toggled, this, &PropertyDock::onCheckBoxPropertyChanged);
                } else if (key == "ThreadSpec") {
                    // [추가] HoleDialog의 나사 규격 콤보박스와 동일한 프리셋 목록 — 직접 입력도 허용.
                    QComboBox *combo = new QComboBox(table);
                    combo->setEditable(true);
                    combo->addItems({"M3x0.5", "M4x0.7", "M5x0.8", "M6x1.0", "M8x1.25",
                                     "M10x1.5", "M12x1.75", "M14x2.0", "M16x2.0", "M20x2.5"});
                    combo->setCurrentText(displayValue.toString());
                    combo->setProperty("key", key);
                    table->setCellWidget(row, 1, combo);
                    connect(combo, &QComboBox::currentTextChanged, this, &PropertyDock::onComboTextPropertyChanged);
                } else if (key == "Prefix" && isDim) {
                    QComboBox *combo = new QComboBox(table);
                    combo->setEditable(true);
                    combo->addItem(tr("없음"), "");
                    combo->addItem(tr("Ø (지름)"), QString::fromUtf8("Ø"));
                    combo->addItem(tr("R (반지름)"), "R");
                    combo->addItem(tr("SR (구면반지름)"), "SR");
                    combo->addItem(tr("M (미터나사)"), "M");
                    combo->setCurrentText(displayValue.toString());
                    combo->setProperty("key", key);
                    table->setCellWidget(row, 1, combo);
                    connect(combo, &QComboBox::currentTextChanged, this, &PropertyDock::onPrefixChanged);
                } else if (key == "Precision" && isDim) {
                    QComboBox *combo = new QComboBox(table);
                    combo->addItem("0", 0);
                    combo->addItem("1", 1);
                    combo->addItem("2", 2);
                    combo->addItem("3", 3);
                    combo->addItem("4", 4);
                    int valIndex = combo->findData(displayValue.toInt());
                    combo->setCurrentIndex(valIndex >= 0 ? valIndex : 0);
                    combo->setProperty("key", key);
                    table->setCellWidget(row, 1, combo);
                    connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                            this, &PropertyDock::onPrecisionChanged);
                } else if (key == "TolType") {
                    QComboBox *combo = new QComboBox(table);
                    combo->addItem(tr("없음"), 0);
                    combo->addItem(tr("대칭 (±)"), 1);
                    combo->addItem(tr("편차 (+/-)"), 2);
                    combo->addItem(tr("한계치"), 3);
                    combo->addItem(tr("끼워맞춤 (Fit)"), 4);
                    int valIndex = combo->findData(displayValue.toInt());
                    combo->setCurrentIndex(valIndex >= 0 ? valIndex : 0);
                    combo->setProperty("key", key);
                    table->setCellWidget(row, 1, combo);
                    connect(combo, static_cast<void (QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                            this, &PropertyDock::onTolTypeChanged);
                } else if (key == "FitCode" && isDim) {
                    // [신규] 끼워맞춤 등급기호 입력 - 자주 쓰는 조합을 프리셋으로 제공하되 직접 입력도 허용
                    QComboBox *combo = new QComboBox(table);
                    combo->setEditable(true);
                    combo->addItem("H7"); combo->addItem("H8"); combo->addItem("H9");
                    combo->addItem("h6"); combo->addItem("js6");
                    combo->addItem("g6"); combo->addItem("f7");
                    combo->addItem("k6"); combo->addItem("m6"); combo->addItem("n6"); combo->addItem("p6");
                    combo->setCurrentText(displayValue.toString());
                    combo->setProperty("key", key);
                    table->setCellWidget(row, 1, combo);
                    connect(combo, &QComboBox::currentTextChanged, this, &PropertyDock::onFitCodeChanged);
                } else if (key == "HideExt1" || key == "HideExt2" || key == "SuppressZero" || key == "SuppressTolZero" || key == "ThousandSep" || key == "FitShowValues" || key == "ThroughHole") {
                    QComboBox *combo = new QComboBox(table);
                    combo->addItem("false");
                    combo->addItem("true");
                    combo->setCurrentText(displayValue.toBool() ? "true" : "false");
                    combo->setProperty("key", key);
                    table->setCellWidget(row, 1, combo);
                    connect(combo, &QComboBox::currentTextChanged, this, &PropertyDock::onBoolPropertyChanged);
                } else if (key == "TolPrecision" || key == "TolUpper" || key == "TolLower") {
                    QDoubleSpinBox *dsb = new QDoubleSpinBox(table);
                    dsb->setRange(-9999.0, 9999.0);
                    if (key == "TolPrecision") {
                        dsb->setSingleStep(1.0);
                        dsb->setDecimals(0);
                    } else {
                        dsb->setSingleStep(0.01);
                        dsb->setDecimals(2);
                    }
                    dsb->setValue(displayValue.toDouble());
                    dsb->setProperty("key", key);
                    table->setCellWidget(row, 1, dsb);
                    connect(dsb, static_cast<void (QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                            this, &PropertyDock::onDoublePropertyChanged);
                } else {
                    QTableWidgetItem *valItem = new QTableWidgetItem(formatValue(displayValue));
                    if (key == "Type" || key == "Length" || key == "DimType" || key == "MLWidth") {
                        valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
                    }
                    table->setItem(row, 1, valItem);
                }
            }
        };

        populateTable(m_topTable, orderTop);
        if (!orderBottom.isEmpty()) {
            m_bottomTable->setVisible(true);
            m_bottomTable->setRowCount(orderBottom.size() + 1);

            QTableWidgetItem *headerItem = new QTableWidgetItem(bottomHeader);
            headerItem->setTextAlignment(Qt::AlignCenter);
            headerItem->setFlags(headerItem->flags() & ~Qt::ItemIsEditable);
            headerItem->setBackground(m_appContext->themeManager()->windowBackground());
            m_bottomTable->setItem(0, 0, headerItem);
            m_bottomTable->setSpan(0, 0, 1, 2);

            populateTable(m_bottomTable, orderBottom, 1);
        } else {
            m_bottomTable->setVisible(false);
        }

        if (!orderExt.isEmpty()) {
            m_extTable->setVisible(true);
            m_extTable->setRowCount(orderExt.size() + 1);

            QTableWidgetItem *headerItem = new QTableWidgetItem(extHeader);
            headerItem->setTextAlignment(Qt::AlignCenter);
            headerItem->setFlags(headerItem->flags() & ~Qt::ItemIsEditable);
            headerItem->setBackground(m_appContext->themeManager()->windowBackground());
            m_extTable->setItem(0, 0, headerItem);
            m_extTable->setSpan(0, 0, 1, 2);

            populateTable(m_extTable, orderExt, 1);
        } else {
            m_extTable->setVisible(false);
        }

        if (!orderExt2.isEmpty()) {
            m_extTable2->setVisible(true);
            m_extTable2->setRowCount(orderExt2.size() + 1);

            QTableWidgetItem *headerItem = new QTableWidgetItem(ext2Header);
            headerItem->setTextAlignment(Qt::AlignCenter);
            headerItem->setFlags(headerItem->flags() & ~Qt::ItemIsEditable);
            headerItem->setBackground(m_appContext->themeManager()->windowBackground());
            m_extTable2->setItem(0, 0, headerItem);
            m_extTable2->setSpan(0, 0, 1, 2);

            populateTable(m_extTable2, orderExt2, 1);
        } else {
            m_extTable2->setVisible(false);
        }

    } else {
        m_bottomTable->setVisible(false);
        m_extTable->setVisible(false);
        m_extTable2->setVisible(false);
        m_topTable->setRowCount(commonProps.size());
        int row = 0;
        QStringList keys = commonProps.keys();
        for (int i = 0; i < keys.size(); ++i) {
            QString originalKey = keys[i];
            QVariant value = commonProps[originalKey];
            QString translatedKey = translateKey(originalKey);

            QTableWidgetItem *keyItem = new QTableWidgetItem(translatedKey);
            keyItem->setData(Qt::UserRole, originalKey);
            keyItem->setFlags(keyItem->flags() & ~Qt::ItemIsEditable);
            m_topTable->setItem(row, 0, keyItem);

            if (originalKey == "Layer") {
                QComboBox *combo = new QComboBox(m_topTable);
                if (m_appContext && m_appContext->layerManager()) {
                    QList<Layer> layers = m_appContext->layerManager()->getAllLayers();
                    for (int j = 0; j < layers.size(); ++j) combo->addItem(layers[j].name());
                }
                combo->setCurrentText(value.toString());
                m_topTable->setCellWidget(row, 1, combo);
                connect(combo, &QComboBox::currentTextChanged, this, &PropertyDock::onLayerChanged);
            } else if (originalKey == "SymbolLayer" || originalKey == "TextLayer") {
                // [수정] 거칠기(SurfaceFinish)의 기호레이어/문자레이어는 객체 자체에 독립적으로
                // 저장되는 속성이라(빈 문자열 = "현재 레이어" 사용), "Layer"와 동일하게
                // 레이어 목록 콤보박스로 편집할 수 있어야 한다. SurfaceFinishDialog와 동일한
                // " 현재 레이어 "(빈 문자열) 항목 규약을 그대로 따른다.
                QComboBox *combo = new QComboBox(m_topTable);
                combo->addItem(tr(" 현재 레이어 "), "");
                if (m_appContext && m_appContext->layerManager()) {
                    QList<Layer> layers = m_appContext->layerManager()->getAllLayers();
                    for (int j = 0; j < layers.size(); ++j) combo->addItem(layers[j].name(), layers[j].name());
                }
                int idx = combo->findData(value.toString());
                combo->setCurrentIndex(idx >= 0 ? idx : 0);
                combo->setProperty("key", originalKey);
                m_topTable->setCellWidget(row, 1, combo);
                connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged),
                        this, &PropertyDock::onLayerNamePropertyChanged);
            } else {
                QTableWidgetItem *valItem = new QTableWidgetItem(formatValue(value));
                if (originalKey == "Type" || originalKey == "Length" || originalKey == "StartX" || originalKey == "StartY") {
                    valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
                }
                m_topTable->setItem(row, 1, valItem);
            }
            row++;
        }
    }

    m_isUpdating = false;
}

void PropertyDock::onPrefixChanged(const QString &text) {
    if (m_isUpdating) return;
    QComboBox *combo = qobject_cast<QComboBox *>(sender());
    if (!combo) return;

    QString key = combo->property("key").toString();
    QString realPrefix = text;

    if (text == tr("Ø (지름)")) realPrefix = QString::fromUtf8("Ø");
    else if (text == tr("R (반지름)")) realPrefix = "R";
    else if (text == tr("SR (구면반지름)")) realPrefix = "SR";
    else if (text == tr("M (미터나사)")) realPrefix = "M";
    else if (text == tr("없음")) realPrefix = "";

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(QString(tr("Change Property: %1")).arg(key));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            QVariant oldValue = ci->getProperties().value(key);
            if (m_appContext && m_appContext->undoStack()) {
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, realPrefix));
            } else {
                ci->setProperty(key, realPrefix);
            }
        }
    }
    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();

    // [추가] 변경 후 속성창(Content 영역) 실시간 새로고침
    QTimer::singleShot(0, this, &PropertyDock::updateDisplay);
}

void PropertyDock::onFitCodeChanged(const QString &text) {
    if (m_isUpdating) return;
    QComboBox *combo = qobject_cast<QComboBox *>(sender());
    if (!combo) return;

    QString key = combo->property("key").toString();

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(QString(tr("Change Property: %1")).arg(key));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            QVariant oldValue = ci->getProperties().value(key);
            // [신규] 등급기호를 고르면 화면에 실제로 보이는 꼬리말(Suffix)에도 그대로
            // 반영한다 - 사용자가 요청한 "등급 선택 시 꼬리말과 연동" 동작.
            // Suffix를 FitCode와 별도의 ChangePropertyCommand로 쌓아야 undo 시에도
            // 각자 원래 값으로 정확히 되돌아간다.
            QVariant oldSuffix = ci->getProperties().value("Suffix");
            if (m_appContext && m_appContext->undoStack()) {
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, text));
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, "Suffix", oldSuffix, text));
            } else {
                ci->setProperty(key, text);
                ci->setProperty("Suffix", text);
            }
        }
    }
    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();

    // [추가] 변경 후 속성창(Content 영역, 공차값 미리보기 등) 실시간 새로고침
    QTimer::singleShot(0, this, &PropertyDock::updateDisplay);
}

// 구멍 연동 로직은 HoleSync(holesync.cpp)로 이동 — 구멍 콜아웃 편집과 공유
void PropertyDock::syncHoleCenterLines(CadItem *ci) {
    HoleSync::syncCenterLines(m_appContext, ci);
}

void PropertyDock::syncThreadedHoleMajorDiameter(CadItem *ci, double newDrillDiameter) {
    HoleSync::syncThreadedMajorDiameter(m_appContext, ci, newDrillDiameter);
}

void PropertyDock::confirmAndSyncHoleSections(CadItem *ci) {
    if (!ci || m_pendingSectionSync.contains(ci)) return;
    const bool wasEmpty = m_pendingSectionSync.isEmpty();
    m_pendingSectionSync.append(ci);
    if (wasEmpty)
        QTimer::singleShot(0, this, &PropertyDock::flushPendingSectionSync);
}

void PropertyDock::syncHoleHeadDiameters(CadItem *ci, bool cb, bool cs) {
    const MyHole *hole = dynamic_cast<const MyHole *>(ci->getEntityData());
    if (!hole) return;

    const bool doCb = cb && hole->hasCounterbore;
    const bool doCs = cs && hole->hasCountersink;
    const double newCb = hole->suggestedCbDiameter();
    const double newCs = hole->suggestedCsDiameter();

    struct Change { const char *key; double value; };
    const Change changes[] = {{"CbDiameter", newCb}, {"CsDiameter", newCs}};
    const bool enabled[] = {doCb, doCs};
    for (int i = 0; i < 2; ++i) {
        if (!enabled[i]) continue;
        QString key = QString::fromLatin1(changes[i].key);
        QVariant oldValue = ci->getProperties().value(key);
        if (m_appContext && m_appContext->undoStack()) {
            m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, changes[i].value));
        } else {
            ci->setProperty(key, changes[i].value);
        }
    }
}

void PropertyDock::flushPendingSectionSync() {
    QList<CadItem *> pending = m_pendingSectionSync;
    m_pendingSectionSync.clear();
    // 그 사이 선택이 바뀌어 삭제됐을 수 있으므로, 처리 전에 현재 선택에 남아 있는 것만 추린다
    // (처리 중에는 하이라이트로 선택이 바뀌므로 먼저 걸러둔다).
    QList<CadItem *> valid;
    for (int i = 0; i < pending.size(); ++i) {
        if (m_selectedItems.contains(pending[i])) valid.append(pending[i]);
    }
    for (int i = 0; i < valid.size(); ++i) {
        HoleSync::confirmAndSyncSections(m_appContext, valid[i], this);
    }
}

void PropertyDock::onComboTextPropertyChanged(const QString &text) {
    if (m_isUpdating) return;
    QComboBox *combo = qobject_cast<QComboBox *>(sender());
    if (!combo) return;

    QString key = combo->property("key").toString();

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(QString(tr("Change Property: %1")).arg(key));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            QVariant oldValue = ci->getProperties().value(key);
            if (m_appContext && m_appContext->undoStack()) {
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, text));
            } else {
                ci->setProperty(key, text);
            }

            // [추가] 나사 규격이 바뀌면 HoleDialog(applyTapDrillDiameter)와 동일한 탭드릴
            // 근사식(호칭지름 - 피치)으로 기본구멍 지름도 함께 갱신한다. 그렇지 않으면
            // 규격 텍스트만 바뀌고 실제 원(기본구멍)은 그대로라 도면과 어긋난다.
            if (key == "ThreadSpec") {
                double majorDia = 0.0, pitch = 0.0;
                if (MyHole::parseThreadSpec(text, majorDia, pitch)) {
                    double drillDia = majorDia - pitch;
                    if (drillDia > 0.0) {
                        QVariant oldDia = ci->getProperties().value("Diameter");
                        if (m_appContext && m_appContext->undoStack()) {
                            m_appContext->undoStack()->push(new ChangePropertyCommand(ci, "Diameter", oldDia, drillDia));
                        } else {
                            ci->setProperty("Diameter", drillDia);
                        }
                    }
                }
                // [추가] 켜져 있는 카운터보어/싱크 지름도 새 규격에 맞춘다
                syncHoleHeadDiameters(ci, true, true);
                // [추가] 나사 규격(호칭지름) 자체도 outerRadius()에 영향을 주므로
                // 중심선 길이를 함께 갱신한다.
                syncHoleCenterLines(ci);
                // [추가] 연결된 구멍 단면이 있으면 확인 후 함께 갱신
                confirmAndSyncHoleSections(ci);
            }
        }
    }
    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();

    QTimer::singleShot(0, this, &PropertyDock::updateDisplay);
}

void PropertyDock::onPrecisionChanged(int index) {
    if (m_isUpdating) return;
    QComboBox *combo = qobject_cast<QComboBox *>(sender());
    if (!combo) return;

    QString key = combo->property("key").toString();
    int intVal = combo->itemData(index).toInt();

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(QString(tr("Change Property: %1")).arg(key));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            QVariant oldValue = ci->getProperties().value(key);
            if (m_appContext && m_appContext->undoStack()) {
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, intVal));
            } else {
                ci->setProperty(key, intVal);
            }
        }
    }

    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();

    // [추가] 소수점 자리수 변경 후, Content 부분에 즉시 반영되도록 속성창 갱신
    QTimer::singleShot(0, this, &PropertyDock::updateDisplay);
}

void PropertyDock::onTolTypeChanged(int index) {
    if (m_isUpdating) return;
    QComboBox *combo = qobject_cast<QComboBox *>(sender());
    if (!combo) return;

    QString key = combo->property("key").toString();
    int intVal = combo->itemData(index).toInt();

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(QString(tr("Change Property: %1")).arg(key));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            QVariant oldValue = ci->getProperties().value(key);
            if (m_appContext && m_appContext->undoStack()) {
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, intVal));
            } else {
                ci->setProperty(key, intVal);
            }
        }
    }

    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();

    // [추가] 속성창 갱신
    QTimer::singleShot(0, this, &PropertyDock::updateDisplay);
}

void PropertyDock::onDoublePropertyChanged(double value) {
    if (m_isUpdating) return;
    QDoubleSpinBox *dsb = qobject_cast<QDoubleSpinBox *>(sender());
    if (!dsb) return;

    QString key = dsb->property("key").toString();

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(QString(tr("Change Property: %1")).arg(key));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            QVariant oldValue = ci->getProperties().value(key);
            if (m_appContext && m_appContext->undoStack()) {
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, value));
            } else {
                ci->setProperty(key, value);
            }

            // [추가] 구멍 지름 스핀박스 전용 — 화살표를 누르는 동안(값이 바뀔 때마다) 매번
            // 탭가공 시 골지름선(ThreadSpec)과 중심선을 조용히(다이얼로그 없이) 갱신한다.
            // 연결된 구멍 단면 확인 다이얼로그는 클릭할 때마다 뜨면 안 되므로, 편집이 끝난
            // 시점(editingFinished, onHoleDiameterEditingFinished)에서 한 번만 띄운다.
            if (key == "Diameter") {
                syncThreadedHoleMajorDiameter(ci, value);
                syncHoleCenterLines(ci);
            }
        }
    }

    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();

    // [추가] 속성창 갱신
    QTimer::singleShot(0, this, &PropertyDock::updateDisplay);
}

// [신규] 구멍 지름 스핀박스(editingFinished)에서만 호출 — onDoublePropertyChanged가 값이
// 바뀔 때마다(화살표 클릭 등) 이미 골지름선/중심선은 갱신해뒀으므로, 여기서는 편집이 끝난
// 시점에 한 번만 연결된 구멍 단면 확인 다이얼로그(confirmAndSyncHoleSections)를 띄운다.
void PropertyDock::onHoleDiameterEditingFinished() {
    if (m_isUpdating) return;
    QDoubleSpinBox *dsb = qobject_cast<QDoubleSpinBox *>(sender());
    if (!dsb) return;

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(tr("Change Property: Diameter"));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            confirmAndSyncHoleSections(ci);
        }
    }

    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();
}

void PropertyDock::onLayerChanged(const QString &layerName) {
    if (m_isUpdating) return;

    if (m_appContext && m_appContext->layerManager()) {
        Layer *layer = m_appContext->layerManager()->getLayer(layerName);
        if (!layer) return;

        QPen pen(layer->color());
        if (m_appContext->lineTypeManager()) {
            QVector<qreal> pattern = m_appContext->lineTypeManager()->getQtDashPattern(layer->linetype());
            if (!pattern.isEmpty()) pen.setDashPattern(pattern);
        }
        pen.setCosmetic(true);
        pen.setWidthF(layer->weight());

        if (m_appContext->undoStack()) m_appContext->undoStack()->beginMacro(tr("Change Layer"));

        for (int i = 0; i < m_selectedItems.size(); ++i) {
            if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
                QString oldLayer = ci->layerName();
                QPen oldPen = ci->pen();
                if (auto *scene = dynamic_cast<MyGraphicsScene *>(ci->scene())) {
                    if (m_appContext->undoStack()) {
                        m_appContext->undoStack()->push(new ChangeLayerCommand(scene, ci, oldLayer, layerName, oldPen, pen));
                    } else {
                        ci->setLayerName(layerName);
                        ci->setPen(pen);
                        scene->reparentItemToLayer(ci, layerName);
                    }
                }
            }
        }
        if (m_appContext->undoStack()) m_appContext->undoStack()->endMacro();
    }
}

void PropertyDock::onLayerNamePropertyChanged(int index) {
    if (m_isUpdating) return;
    QComboBox *combo = qobject_cast<QComboBox *>(sender());
    if (!combo) return;

    QString key = combo->property("key").toString();
    QString layerValue = combo->itemData(index).toString(); // " 현재 레이어 " 선택 시 빈 문자열

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(QString(tr("Change Property: %1")).arg(key));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            QVariant oldValue = ci->getProperties().value(key);
            if (m_appContext && m_appContext->undoStack()) {
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, layerValue));
            } else {
                ci->setProperty(key, layerValue);
            }

            // [추가] Hole의 BaseLayer/ThreadLayer/CbLayer/CsLayer가 바뀌면 단면 선의 레이어
            // 배치도 달라지므로, 연결된 구멍 단면이 있으면 확인 후 함께 갱신한다. Hole이 아니면
            // (예: SurfaceFinish의 SymbolLayer/TextLayer) 내부에서 조용히 무시된다.
            confirmAndSyncHoleSections(ci);
        }
    }

    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();

    // [추가] 속성창 갱신
    QTimer::singleShot(0, this, &PropertyDock::updateDisplay);
}

void PropertyDock::onBoolPropertyChanged(const QString &value) {
    if (m_isUpdating) return;
    QComboBox *combo = qobject_cast<QComboBox *>(sender());
    if (!combo) return;

    QString key = combo->property("key").toString();
    bool boolVal = (value == "true");

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(QString(tr("Change Property: %1")).arg(key));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            QVariant oldValue = ci->getProperties().value(key);
            if (m_appContext && m_appContext->undoStack()) {
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, boolVal));
            } else {
                ci->setProperty(key, boolVal);
            }

            // [추가] Hole의 ThroughHole(관통 여부)이 바뀌면 단면 형상(재료두께/탭깊이 처리)도
            // 달라지므로, 연결된 구멍 단면이 있으면 확인 후 함께 갱신한다. Hole이 아니면(예:
            // Dimension의 HideExt1 등) 내부에서 조용히 무시된다.
            confirmAndSyncHoleSections(ci);
        }
    }
    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();

    // [추가] Hole의 ThroughHole 등은 값이 바뀌면 관련 필드(Depth 행 표시 여부 등)가
    // 함께 바뀌어야 하므로 속성창을 다시 그린다.
    QTimer::singleShot(0, this, &PropertyDock::updateDisplay);
}

void PropertyDock::onCheckBoxPropertyChanged(bool checked) {
    if (m_isUpdating) return;
    QCheckBox *checkbox = qobject_cast<QCheckBox *>(sender());
    if (!checkbox) return;

    QString key = checkbox->property("key").toString();

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(QString(tr("Change Property: %1")).arg(key));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            QVariant oldValue = ci->getProperties().value(key);
            if (m_appContext && m_appContext->undoStack()) {
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, checked));
            } else {
                ci->setProperty(key, checked);
            }

            // [추가] 카운터보어/카운터싱크를 켜면 지름을 이 구멍의 탭 규격에 맞춰 채운다.
            // 탭가공을 켜고 끌 때도 켜져 있는 카운터보어/싱크 지름을 규격에 맞춰 다시 맞춘다.
            if (checked && key == "HasCounterbore") syncHoleHeadDiameters(ci, true, false);
            else if (checked && key == "HasCountersink") syncHoleHeadDiameters(ci, false, true);
            else if (key == "IsThreaded") syncHoleHeadDiameters(ci, true, true);

            // [추가] 탭가공/카운터보어/카운터싱크를 켜고 끄면 outerRadius()가 바뀌므로
            // 중심선 길이도 함께 갱신한다.
            syncHoleCenterLines(ci);

            // [수정] 탭가공/카운터보어/카운터싱크는 켜고 끄는 어느 쪽이든 outerRadius()·
            // 단면 형상이 달라지므로 그 자리에서 바로 단면 갱신 여부를 물어야 한다.
            // 이전에는 켤 때(checked=true) 이 호출을 건너뛰어, 사용자가 다른 속성 셀을
            // 건드리지 않는 한 onPropertyChanged가 트리거되지 않아 평면(HoleItem)만 갱신되고
            // 단면 LineItem은 옛 상태로 남는 버그가 있었다.
            // (켠 직후 카운터보어/카운터싱크 지름·깊이는 기본값이지만, 이후 실제 값을
            //  입력·확정하면 onPropertyChanged에서 다시 confirmAndSyncHoleSections가
            //  호출되므로 단면은 최종값으로 재갱신된다.)
            confirmAndSyncHoleSections(ci);
        }
    }
    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();

    // [추가] 탭가공/카운터보어/카운터싱크 On-Off에 따라 아래 그룹 표가 즉시
    // 나타나거나 사라져야 하므로 속성창을 다시 그린다.
    QTimer::singleShot(0, this, &PropertyDock::updateDisplay);
}

void PropertyDock::onPropertyChanged(int row, int column) {
    if (m_isUpdating || column != 1) return;

    QTableWidget *table = qobject_cast<QTableWidget *>(sender());
    if (!table) return;

    QString key = table->item(row, 0)->data(Qt::UserRole).toString();
    QString value = table->item(row, 1)->text();

    if (value == "*Varies*") return;

    if (m_appContext && m_appContext->undoStack()) {
        m_appContext->undoStack()->beginMacro(QString(tr("Change Property: %1")).arg(key));
    }

    for (int i = 0; i < m_selectedItems.size(); ++i) {
        if (CadItem *ci = dynamic_cast<CadItem *>(m_selectedItems[i])) {
            // [추가] 카운터보어/카운터싱크 지름은 나사 지름(탭가공된 구멍이면 나사 호칭지름,
            // 아니면 기본구멍 지름)보다 커야 한다 — 그 이하면 나사머리/원뿔이 구멍 벽 밖으로
            // 나가지 못해 실제로 만들 수 없는 형상이 된다. 잘못된 값은 적용하지 않고 되돌린다.
            if (key == "CbDiameter" || key == "CsDiameter") {
                const MyHole *holeData = dynamic_cast<const MyHole *>(ci->getEntityData());
                if (holeData) {
                    double minDiameter = holeData->isThreaded
                                              ? holeData->threadMajorRadius() * 2.0
                                              : holeData->diameter;
                    double newDiameter = value.toDouble();
                    if (newDiameter <= minDiameter) {
                        QMessageBox::warning(
                            this, tr("입력 오류"),
                            tr("%1 지름(%2)은 %3 지름(%4)보다 커야 합니다.")
                                .arg(key == "CbDiameter" ? tr("카운터보어") : tr("카운터싱크"))
                                .arg(newDiameter, 0, 'f', 2)
                                .arg(holeData->isThreaded ? tr("나사") : tr("기본구멍"))
                                .arg(minDiameter, 0, 'f', 2));
                        continue;
                    }
                }
            }

            // [추가] 구멍 콜아웃의 Content 편집은 구멍 사양으로 해석해 연결된 구멍에 반영
            if (key == "Content") {
                if (LeaderItem *leader = dynamic_cast<LeaderItem *>(ci)) {
                    if (HoleCalloutLink::applyTextEdit(m_appContext, leader, value, this))
                        continue;
                }
            }

            // [수정] "Content"는 화면에 합쳐진 문자열이 표시되는 키라서, undo용 예전 값도
            // 합쳐지기 전 순수값(RawValue)에서 가져와야 undo 시 중복 표기가 재발하지 않는다.
            // RawValue가 없는 객체(지시선·문자 등)는 Content 자체가 순수값이다 — 없는 키를 읽으면
            // 빈 값이 저장돼 undo 시 텍스트가 사라진다.
            const QMap<QString, QVariant> oldProps = ci->getProperties();
            QString oldKey = (key == "Content" && oldProps.contains("RawValue")) ? "RawValue" : key;
            QVariant oldValue = oldProps.value(oldKey);

            // [추가] BOXTEXT 박스가 있는 글자면 변경 전 박스를 기억해 두었다가
            // 내용/글꼴/크기/회전 변경 후 박스를 글자에 맞춰 조정 (Setup "박스크기 연동" 켜진 경우)
            TextItem *textItem = dynamic_cast<TextItem *>(ci);
            BoxTextUtil::BoxFollowState boxState;
            if (textItem) boxState = BoxTextUtil::captureBox(textItem);

            if (m_appContext && m_appContext->undoStack()) {
                m_appContext->undoStack()->push(new ChangePropertyCommand(ci, key, oldValue, value));
            } else {
                ci->setProperty(key, value);
            }

            if (textItem) {
                BoxTextUtil::followText(m_appContext ? m_appContext->undoStack() : nullptr,
                                        textItem, boxState);
            }

            // [추가] 탭가공된 구멍의 기본(드릴)지름을 바꾸면 골지름선(나사 호칭지름 원/호)도
            // 함께 움직여야 하므로, 기존 규격의 피치를 유지한 채 ThreadSpec을 다시 계산한다.
            if (key == "Diameter") {
                syncThreadedHoleMajorDiameter(ci, value.toDouble());
            }

            // [추가] 기본구멍/카운터보어/카운터싱크 지름을 직접 입력해도 outerRadius()가
            // 바뀌므로 중심선 길이를 함께 갱신한다.
            if (key == "Diameter" || key == "CbDiameter" || key == "CsDiameter") {
                syncHoleCenterLines(ci);
            }

            // [추가] 구멍의 위치(CenterX/CenterY)는 단면과 무관(단면은 별도 배치 위치를 씀)
            // 하므로 제외하고, 그 외 스펙(지름/깊이/공차/탭·카운터보어·카운터싱크 값 등)이
            // 바뀌면 연결된 구멍 단면이 있는지 확인 후 함께 갱신한다.
            if (key != "CenterX" && key != "CenterY") {
                confirmAndSyncHoleSections(ci);
            }
        }
    }
    if (m_appContext && m_appContext->undoStack()) m_appContext->undoStack()->endMacro();

    // [추가] 텍스트가 바뀐 후 다른 셀에 영향을 줄 수 있으므로 갱신
    QTimer::singleShot(0, this, &PropertyDock::updateDisplay);
}

void PropertyDock::clearTables() {
    m_topTable->setRowCount(0);
    m_bottomTable->setRowCount(0);
    m_extTable->setRowCount(0);
    m_extTable2->setRowCount(0);
    if (m_deleteSymbolBtn)
        m_deleteSymbolBtn->setVisible(false);
}

QString PropertyDock::formatValue(const QVariant &val) {
    if (val.userType() == QMetaType::Double) return QString::number(val.toDouble(), 'f', 2);
    if (val.toString() == "Block") return tr("블록");
    if (val.toString() == "Line") return tr("선");
    if (val.toString() == "Circle") return tr("원");
    if (val.toString() == "Arc") return tr("호");
    if (val.toString() == "Polyline") return tr("폴리라인");
    if (val.toString() == "Text") return tr("문자");
    if (val.toString() == "BaseDimension" || val.toString() == "Dimension") return tr("치수선");
    if (val.toString() == "LinearDimension" || val.toString() == "Linear") return tr("선형 치수");
    if (val.toString() == "RadialDimension" || val.toString() == "Radial") return tr("원형/반지름 치수");
    if (val.toString() == "DiametricDimension" || val.toString() == "Diametric") return tr("지름 치수");
    if (val.toString() == "AngularDimension" || val.toString() == "Angular") return tr("각도 치수");
    if (val.toString() == "HalfDimension") return tr("반치수");
    if (val.toString() == "Leader") return tr("지시선");
    if (val.toString() == "Hole") return tr("구멍");
    return val.toString();
}

QString PropertyDock::formatSymbolType(const QString &type)
{
    if (type == "PlanDoor")             return QObject::tr("평면도어");
    if (type == "PlanDoorDouble")       return QObject::tr("쌍여닫이");
    if (type == "PlanDoorBiSwing")      return QObject::tr("양방향여닫이");
    if (type == "PlanDoorDoubleBiSwing") return QObject::tr("양방쌍여닫이");
    if (type == "PlanRevolvingDoor")    return QObject::tr("회전문");
    if (type == "PlanDoorSimple")       return QObject::tr("단순문");
    if (type == "PlanDoorSimpleDouble") return QObject::tr("단순쌍여닫이");
    if (type == "PlanDoorSimpleBiSwing") return QObject::tr("단순양방여닫이");
    if (type == "PlanDoorSimpleDoubleBiSwing") return QObject::tr("단순양방쌍여닫이");
    if (type == "PlanWindow")           return QObject::tr("평면창문");
    return type;
}

void PropertyDock::showSymbolProperties(const SymbolInfo &info)
{
    m_bottomTable->setVisible(false);
    m_extTable->setVisible(false);
    m_extTable2->setVisible(false);

    if (m_deleteSymbolBtn) {
        const bool isAnyDoor = info.type == "PlanDoor" || info.type == "PlanDoorDouble" ||
                              info.type == "PlanDoorBiSwing" || info.type == "PlanDoorDoubleBiSwing" ||
                              info.type == "PlanRevolvingDoor" || PlanDoorSimpleTool::isSimpleSymbolType(info.type);
        const QString label = isAnyDoor ? tr("문 삭제") :
                              (info.type == "PlanWindow") ? tr("창문 삭제") :
                                                             tr("삭제");
        m_deleteSymbolBtn->setText(label);
        m_deleteSymbolBtn->setVisible(true);
    }

    // ── 공통 2행: 유형·이름 (읽기 전용) ─────────────────────────────────────
    struct RowDef { QString key; QString label; QString value; bool editable; };
    QList<RowDef> rows;
    rows << RowDef{"Type", tr("유형"), formatSymbolType(info.type), false};
    rows << RowDef{"Name", tr("이름"), info.name, false};

    const bool isAnyDoorType = info.type == "PlanDoor" || info.type == "PlanDoorDouble" ||
                               info.type == "PlanDoorBiSwing" || info.type == "PlanDoorDoubleBiSwing" ||
                               info.type == "PlanRevolvingDoor";
    if (info.type == "PlanDoor" || info.type == "PlanDoorBiSwing") {
        rows << RowDef{"H",         tr("문 너비"),  QString::number(info.props.value("H").toDouble(), 'f', 0), true};
        rows << RowDef{"V",         tr("문틀 폭"),  QString::number(info.props.value("V").toDouble(), 'f', 0), true};
        rows << RowDef{"FrameTypeHdr", tr("문틀 형태"), "", false};  // 제목 행 (2열 병합)
        rows << RowDef{"FrameType",    "",             "", false};  // 아이콘 행 4종
    } else if (info.type == "PlanDoorDouble" || info.type == "PlanDoorDoubleBiSwing") {
        rows << RowDef{"H1",        tr("문1 너비(시작점)"), QString::number(info.props.value("H1").toDouble(), 'f', 0), true};
        rows << RowDef{"H2",        tr("문2 너비"),         QString::number(info.props.value("H2").toDouble(), 'f', 0), true};
        rows << RowDef{"V",         tr("문틀 폭"),          QString::number(info.props.value("V").toDouble(), 'f', 0), true};
        rows << RowDef{"FrameTypeHdr", tr("문틀 형태"), "", false};  // 제목 행 (2열 병합)
        rows << RowDef{"FrameType",    "",             "", false};  // 아이콘 행 4종
    } else if (PlanDoorSimpleTool::isSimpleSymbolType(info.type)) {
        // 단순문: 문 너비만 편집 (벽 두께는 배치 시 측정값 고정, 문틀·형태 없음)
        rows << RowDef{"H",         tr("문 너비"),  QString::number(info.props.value("H").toDouble(), 'f', 0), true};
        rows << RowDef{"V",         tr("벽 두께"),  QString::number(info.props.value("V").toDouble(), 'f', 0), false};
    } else if (info.type == "PlanRevolvingDoor") {
        // 편집 UI(rebuildSymbol 연동)는 아직 미구현 — 우선 읽기 전용으로 표시
        rows << RowDef{"H",         tr("문 크기"),   QString::number(info.props.value("H").toDouble(), 'f', 0), false};
        rows << RowDef{"V",         tr("문틀/벽 두께"), QString::number(info.props.value("V").toDouble(), 'f', 0), false};
        rows << RowDef{"WingCount", tr("날개 개수"), QString::number(info.props.value("WingCount").toInt()), false};
    } else if (info.type == "PlanWindow") {
        rows << RowDef{"W",  tr("창문 너비"),  QString::number(info.props.value("W").toDouble(),  'f', 0), true};
        const int winVariant = info.props.value("Variant", 0).toInt();
        if (winVariant == 1 || winVariant == 2)   // 상단부만/연결형은 단창·복창 구분 없음
            rows << RowDef{"WinDouble", tr("창 구성"), "", false};
        if (winVariant >= 1 && winVariant <= 3) {   // 전면창(연결형)은 유리선·짝 넓이 없음
            const int sh = qBound(2, info.props.value("Sashes", 2).toInt(), 4);
            const QString sashLabel = (sh == 4) ? tr("안쪽창 넓이 (0=동일)")
                                    : (sh == 3) ? tr("중앙창 넓이 (0=동일)")
                                                : tr("슬라이딩 창 넓이 (0=동일)");
            rows << RowDef{"Glass", tr("유리 표현"), "", false};
            rows << RowDef{"SashW", sashLabel,
                           QString::number(info.props.value("SashW").toDouble(), 'f', 0), true};
        }
        rows << RowDef{"WinShapeHdr", tr("창문 형태"), "", false};
        rows << RowDef{"WinShape",    "",              "", false};
    }

    // 문틀 형태 아래에 문 형태 (제목 행 + 아이콘 행)
    if (isAnyDoorType || PlanDoorSimpleTool::isSimpleSymbolType(info.type)) {
        rows << RowDef{"DoorShapeHdr", tr("문 형태"), "", false};
        rows << RowDef{"DoorShape",    "",            "", false};
    }

    m_topTable->clearSpans();
    m_topTable->setRowCount(rows.size());
    for (int i = 0; i < rows.size(); ++i) {
        const RowDef &r = rows.at(i);
        const bool isIconRow = (r.key == "DoorShape" || r.key == "FrameType" || r.key == "WinShape");
        const bool isHdrRow  = r.key.endsWith("Hdr");
        m_topTable->setRowHeight(i, m_topTable->verticalHeader()->defaultSectionSize());  // 이전 아이콘 행 높이 잔재 초기화

        if (isIconRow) {
            m_topTable->setSpan(i, 0, 1, 2);   // 아이콘 행은 2열 병합 (라벨 없음)
        } else {
            QTableWidgetItem *labelItem = new QTableWidgetItem(r.label);
            labelItem->setFlags(labelItem->flags() & ~Qt::ItemIsEditable);
            if (isHdrRow) {
                QFont f = labelItem->font();
                f.setBold(true);
                labelItem->setFont(f);
                m_topTable->setSpan(i, 0, 1, 2);   // 제목 행도 2열 병합
            }
            m_topTable->setItem(i, 0, labelItem);
        }
        if (isHdrRow)
            continue;

        if (r.key == "DoorShape") {
            // 탭메뉴(평면도어)와 동일한 아이콘: 일반 문 5종 / 단순문 4종 (현재 계열 안에서 치환)
            static const char *kIcons[5] = {
                ":/icons/sdoor.png", ":/icons/ddoor.png", ":/icons/ssdoor.png",
                ":/icons/sddoor.png", ":/icons/rdoor.png" };
            static const char *kTypes[5] = {
                "PlanDoor", "PlanDoorDouble", "PlanDoorBiSwing",
                "PlanDoorDoubleBiSwing", "PlanRevolvingDoor" };
            static const char *kSimpleTypes[4] = {
                "PlanDoorSimple", "PlanDoorSimpleDouble", "PlanDoorSimpleBiSwing",
                "PlanDoorSimpleDoubleBiSwing" };
            // 단순문 계열은 평면도어 탭의 단순문 아이콘 사용 (appcommandregistry.cpp 와 동일)
            static const char *kSimpleIcons[4] = {
                ":/icons/sNdoor.png", ":/icons/dNdoor.png", ":/icons/sNsdoor.png",
                ":/icons/sNddoor.png" };
            const QString names[5] = { tr("외여닫이"), tr("쌍여닫이"), tr("양방여닫이"),
                                       tr("양방쌍여닫이"), tr("회전문") };
            const bool simpleFamily = PlanDoorSimpleTool::isSimpleSymbolType(info.type);
            const int  count = simpleFamily ? 4 : 5;
            const int IC = 40;
            QWidget     *w   = new QWidget(m_topTable);
            QHBoxLayout *row = new QHBoxLayout(w);
            row->setContentsMargins(2, 2, 2, 2);
            row->setSpacing(4);
            QButtonGroup *grp = new QButtonGroup(w);
            grp->setExclusive(true);
            for (int t = 0; t < count; ++t) {
                QToolButton *btn = new QToolButton(w);
                btn->setIcon(QIcon(QString::fromLatin1(simpleFamily ? kSimpleIcons[t] : kIcons[t])));
                btn->setIconSize(QSize(IC, IC));
                btn->setCheckable(true);
                btn->setToolTip(simpleFamily ? tr("단순") + names[t] : names[t]);
                btn->setChecked(info.type == QLatin1String(simpleFamily ? kSimpleTypes[t] : kTypes[t]));
                grp->addButton(btn, t);
                row->addWidget(btn);
            }
            row->addStretch();
            connect(grp, SIGNAL(idClicked(int)), this, SLOT(onSymbolDoorShapeClicked(int)));
            m_topTable->setCellWidget(i, 0, w);
            m_topTable->setRowHeight(i, IC + 14);
        } else if (r.key == "WinShape") {
            // 평면창문 탭과 동일한 아이콘 7종 (2/3/4 미서기, 2/3/4 상미서기, 전면창)
            static const char *kWinIcons[7] = {
                ":/icons/s-win1-1.png", ":/icons/s-win3-1.png", ":/icons/s-win4-1.png",
                ":/icons/s-win1.png",   ":/icons/s-win3.png",   ":/icons/s-win4.png",
                ":/icons/s-win5.png" };
            const QString winNames[7] = { tr("2미서기"), tr("3미서기"), tr("4미서기"),
                                          tr("2상미서기"), tr("3상미서기"), tr("4상미서기"),
                                          tr("전면창") };
            const int wv  = info.props.value("Variant", 0).toInt();
            const int wsh = qBound(2, info.props.value("Sashes", 2).toInt(), 4);
            int cur = -1;
            if (wv == 1 || wv == 2) cur = wsh - 2;
            else if (wv == 3)       cur = 3 + wsh - 2;
            else if (wv == 4)       cur = 6;
            const int IC = 40;
            QWidget     *w   = new QWidget(m_topTable);
            QGridLayout *grid = new QGridLayout(w);
            grid->setContentsMargins(2, 2, 2, 2);
            grid->setSpacing(4);
            QButtonGroup *grp = new QButtonGroup(w);
            grp->setExclusive(true);
            for (int t = 0; t < 7; ++t) {
                QToolButton *btn = new QToolButton(w);
                btn->setIcon(QIcon(QString::fromLatin1(kWinIcons[t])));
                btn->setIconSize(QSize(IC, IC));
                btn->setCheckable(true);
                btn->setToolTip(winNames[t]);
                btn->setChecked(t == cur);
                grp->addButton(btn, t);
                grid->addWidget(btn, t / 4, t % 4);
            }
            grid->setColumnStretch(4, 1);
            connect(grp, SIGNAL(idClicked(int)), this, SLOT(onSymbolWindowShapeClicked(int)));
            m_topTable->setCellWidget(i, 0, w);
            m_topTable->setRowHeight(i, (IC + 8) * 2 + 10);
        } else if (r.key == "Glass") {
            QCheckBox *cb = new QCheckBox(tr("유리 표현선"), m_topTable);
            cb->setChecked(info.props.value("Glass", 0).toInt() != 0);
            connect(cb, SIGNAL(toggled(bool)), this, SLOT(onSymbolWindowGlassToggled(bool)));
            m_topTable->setCellWidget(i, 1, cb);
        } else if (r.key == "WinDouble") {
            QComboBox *cb = new QComboBox(m_topTable);
            cb->addItem(tr("단창"));
            cb->addItem(tr("복창"));
            cb->setCurrentIndex(info.props.value("Variant", 1).toInt() == 2 ? 1 : 0);
            connect(cb, SIGNAL(currentIndexChanged(int)), this, SLOT(onSymbolWindowDoubleChanged(int)));
            m_topTable->setCellWidget(i, 1, cb);
        } else if (r.key == "FrameType") {
            // 아이콘 버튼 4개: 문틀 형태 (설정 다이얼로그와 동일한 썸네일)
            const int ICW = 38, ICH = 56;
            QWidget     *w    = new QWidget(m_topTable);
            QHBoxLayout *row  = new QHBoxLayout(w);
            row->setContentsMargins(2, 2, 2, 2);
            row->setSpacing(4);
            QButtonGroup *grp = new QButtonGroup(w);
            grp->setExclusive(true);
            const int cur = qBound(0, info.props.value("FrameType").toInt(), 3);
            for (int t = 0; t < 4; ++t) {
                QToolButton *btn = new QToolButton(w);
                btn->setIcon(QIcon(makeJambIcon(t, ICW, ICH)));
                btn->setIconSize(QSize(ICW, ICH));
                btn->setCheckable(true);
                btn->setToolTip(tr("Type %1").arg(t + 1));
                btn->setChecked(t == cur);
                grp->addButton(btn, t);
                row->addWidget(btn);
            }
            row->addStretch();
            connect(grp, SIGNAL(idClicked(int)), this, SLOT(onSymbolFrameTypeChanged(int)));
            m_topTable->setCellWidget(i, 0, w);
            m_topTable->setRowHeight(i, ICH + 14);
        } else if (r.editable) {
            // QDoubleSpinBox: 수치 편집
            QDoubleSpinBox *sb = new QDoubleSpinBox(m_topTable);
            sb->setRange(r.key == "SashW" ? 0.0 : 1.0, 99999.0);
            sb->setDecimals(0);
            sb->setSuffix(" mm");
            sb->setValue(r.value.toDouble());
            sb->setProperty("key", r.key);
            connect(sb, SIGNAL(editingFinished()), this, SLOT(onSymbolSpinboxFinished()));
            m_topTable->setCellWidget(i, 1, sb);
        } else {
            QTableWidgetItem *valItem = new QTableWidgetItem(r.value);
            valItem->setFlags(valItem->flags() & ~Qt::ItemIsEditable);
            m_topTable->setItem(i, 1, valItem);
        }
    }
}

// ── 스핀박스 편집 완료 ────────────────────────────────────────────────────────
void PropertyDock::onSymbolSpinboxFinished()
{
    if (m_currentGroupId.isEmpty() || !m_appContext)
        return;

    QDoubleSpinBox *sb = qobject_cast<QDoubleSpinBox *>(sender());
    if (!sb)
        return;

    const QString key    = sb->property("key").toString();
    const double  newVal = sb->value();

    MyGraphicsScene *scene = nullptr;
    if (!m_selectedItems.isEmpty())
        scene = dynamic_cast<MyGraphicsScene *>(m_selectedItems.first()->scene());
    if (!scene)
        return;

    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(m_currentGroupId))
        return;

    SymbolInfo oldInfo = reg->symbolInfo(m_currentGroupId);
    SymbolInfo newInfo = oldInfo;
    newInfo.props[key] = newVal;

    // 쌍여닫이: 문1/문2 너비 편집 시 전체 폭(H = H1+H2)을 함께 갱신
    // (H 는 벽 트림/래치 끝점 조정 로직에서 공통으로 사용됨)
    if ((oldInfo.type == "PlanDoorDouble" || oldInfo.type == "PlanDoorDoubleBiSwing") &&
        (key == "H1" || key == "H2")) {
        newInfo.props["H"] = newInfo.props.value("H1").toDouble()
                            + newInfo.props.value("H2").toDouble();
    }

    rebuildSymbol(scene, m_currentGroupId, oldInfo, newInfo);
}

// ── 문틀 형태 아이콘 버튼 변경 ────────────────────────────────────────────────────────
void PropertyDock::onSymbolFrameTypeChanged(int index)
{
    if (m_currentGroupId.isEmpty() || !m_appContext)
        return;

    const int newType = index;   // QButtonGroup::idClicked 의 id = 문틀 Type

    MyGraphicsScene *scene = nullptr;
    if (!m_selectedItems.isEmpty())
        scene = dynamic_cast<MyGraphicsScene *>(m_selectedItems.first()->scene());
    if (!scene)
        return;

    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(m_currentGroupId))
        return;

    SymbolInfo oldInfo = reg->symbolInfo(m_currentGroupId);
    SymbolInfo newInfo = oldInfo;
    newInfo.props["FrameType"] = newType;

    rebuildSymbol(scene, m_currentGroupId, oldInfo, newInfo);
}

// ── 문 형태 치환 (아이콘 클릭 → 문 너비 입력 → 형태 교체) ───────────────────────
void PropertyDock::onSymbolDoorShapeClicked(int shapeIndex)
{
    static const char *kTypes[5] = {
        "PlanDoor", "PlanDoorDouble", "PlanDoorBiSwing",
        "PlanDoorDoubleBiSwing", "PlanRevolvingDoor" };
    static const char *kSimpleTypes[4] = {
        "PlanDoorSimple", "PlanDoorSimpleDouble", "PlanDoorSimpleBiSwing",
        "PlanDoorSimpleDoubleBiSwing" };
    if (shapeIndex < 0 || shapeIndex > 4)
        return;
    if (m_currentGroupId.isEmpty() || !m_appContext)
        return;

    MyGraphicsScene *scene = nullptr;
    if (!m_selectedItems.isEmpty())
        scene = dynamic_cast<MyGraphicsScene *>(m_selectedItems.first()->scene());
    if (!scene)
        return;

    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(m_currentGroupId))
        return;

    const SymbolInfo oldInfo = reg->symbolInfo(m_currentGroupId);
    const bool simpleFamily = PlanDoorSimpleTool::isSimpleSymbolType(oldInfo.type);
    if (simpleFamily && shapeIndex > 3)
        return;
    const QString newType = QString::fromLatin1(simpleFamily ? kSimpleTypes[shapeIndex]
                                                             : kTypes[shapeIndex]);
    if (newType == oldInfo.type)
        return;   // 같은 형태 재클릭 — 아이콘 체크 상태는 그대로 유지됨

    // 현재 전체 폭 (H1+H2 형은 H 에 합계가 저장되어 있음)
    double curW = oldInfo.props.value("H").toDouble();
    if (curW <= 0.0)
        curW = oldInfo.props.value("H1").toDouble() + oldInfo.props.value("H2").toDouble();
    if (curW <= 0.0)
        curW = 900.0;

    const bool toDouble   = (newType == "PlanDoorDouble" || newType == "PlanDoorDoubleBiSwing" ||
                             newType == "PlanDoorSimpleDouble" || newType == "PlanDoorSimpleDoubleBiSwing");
    const bool toRevolving = (newType == "PlanRevolvingDoor");
    QString label = tr("문 너비 (mm):");
    if (toDouble)
        label = tr("문 너비 (mm) — 전체 폭이며 두 문짝으로 균등 분할됩니다:");
    else if (toRevolving)
        label = tr("문 너비 (mm) — 회전문 개구부 전체 폭:");

    bool ok = false;
    const double w = QInputDialog::getDouble(
        this, tr("문 형태 변경"), label, curW,
        toRevolving ? 800.0 : 100.0, 20000.0, 0, &ok);
    if (!ok)
        return;

    SymbolInfo newInfo = oldInfo;
    newInfo.type = newType;
    newInfo.props["H"] = w;
    newInfo.props.remove("H1");
    newInfo.props.remove("H2");
    newInfo.props.remove("WingCount");
    if (simpleFamily) {
        // 단순문 계열: H 만 사용 (쌍여닫이류는 재빌드 시 H/2 균등 분할)
    } else if (toDouble) {
        newInfo.props["H1"] = w / 2.0;
        newInfo.props["H2"] = w / 2.0;
    } else if (toRevolving) {
        newInfo.props["WingCount"] = PlanRevolvingDoorTool::wingCount();
    }

    rebuildSymbol(scene, m_currentGroupId, oldInfo, newInfo);
}

// ── 유리 표현선 on/off ──────────────────────────────────────────────────────
void PropertyDock::onSymbolWindowGlassToggled(bool on)
{
    if (m_currentGroupId.isEmpty() || !m_appContext)
        return;

    MyGraphicsScene *scene = nullptr;
    if (!m_selectedItems.isEmpty())
        scene = dynamic_cast<MyGraphicsScene *>(m_selectedItems.first()->scene());
    if (!scene)
        return;

    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(m_currentGroupId))
        return;

    const SymbolInfo oldInfo = reg->symbolInfo(m_currentGroupId);
    if (oldInfo.type != "PlanWindow" || (oldInfo.props.value("Glass", 0).toInt() != 0) == on)
        return;

    SymbolInfo newInfo = oldInfo;
    newInfo.props["Glass"] = on ? 1 : 0;
    rebuildSymbol(scene, m_currentGroupId, oldInfo, newInfo);
}

// ── 단창/복창 전환 ───────────────────────────────────────────────────────────
void PropertyDock::onSymbolWindowDoubleChanged(int index)
{
    if (m_currentGroupId.isEmpty() || !m_appContext)
        return;

    MyGraphicsScene *scene = nullptr;
    if (!m_selectedItems.isEmpty())
        scene = dynamic_cast<MyGraphicsScene *>(m_selectedItems.first()->scene());
    if (!scene)
        return;

    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(m_currentGroupId))
        return;

    const SymbolInfo oldInfo = reg->symbolInfo(m_currentGroupId);
    const int newVariant = (index == 1) ? 2 : 1;
    if (oldInfo.type != "PlanWindow" || oldInfo.props.value("Variant", 0).toInt() == newVariant)
        return;

    SymbolInfo newInfo = oldInfo;
    newInfo.props["Variant"] = newVariant;
    rebuildSymbol(scene, m_currentGroupId, oldInfo, newInfo);
}

// 창 두께 / 벽면 기준 창틀 돌출량 (PlanWindowTool::windowThickness / wallProtrusion 과 동일 규칙)
static void windowThicknessAndProt(const QVariantMap &pr, double &outT, double &outProt)
{
    const int v = pr.value("Variant", 0).toInt();
    if (v >= 3)
        outT = pr.value("Win5").toDouble();
    else if (v >= 1)
        outT = pr.value("Win3").toDouble() + pr.value("Win5").toDouble();
    else
        outT = pr.value("D1").toDouble() + pr.value("D2").toDouble();
    outProt = (v >= 1) ? pr.value("Win1").toDouble() : 0.0;
}

// 형태 교체로 창 두께가 바뀌면, 배치 시 선택한 벽 위치 옵션(안쪽/중앙/바깥)을 새 두께로 재적용한다.
// 옵션은 props["WallPosMode"] 에 저장되며, 없는 예전 창은 현재 벽 상대 위치로 판별한다.
// 벽면의 월드 위치는 고정하고 창 원점만 로컬 Y 방향으로 이동 → ox/oy/wallNearLy/wallFarLy/trimY 갱신.
static bool reapplyWindowWallPosition(const SymbolInfo &oldInfo, SymbolInfo &newInfo, QWidget *parent)
{
    double tOld = 0.0, pOld = 0.0, tNew = 0.0, pNew = 0.0;
    windowThicknessAndProt(oldInfo.props, tOld, pOld);
    windowThicknessAndProt(newInfo.props, tNew, pNew);

    const double near0 = oldInfo.props.value("wallNearLy").toDouble();
    const double far0  = oldInfo.props.value("wallFarLy").toDouble();
    const double span  = far0 - near0;
    const double tol   = 1.0;

    int mode = oldInfo.props.value("WallPosMode", 0).toInt();
    if (mode == 0 && !oldInfo.props.contains("WallPosMode") && span > 0.0) {
        if (qAbs(near0 - pOld) < tol)                          mode = WallPositionDialog::Inner;
        else if (qAbs(far0 - (tOld - pOld)) < tol)             mode = WallPositionDialog::Outer;
        else if (qAbs((near0 + far0) / 2.0 - tOld / 2.0) < tol) mode = WallPositionDialog::Center;
    }
    newInfo.props["WallPosMode"] = mode;

    // 옵션 미적용(벽 미감지·벽두께 사용·판별 불가)이면 위치 유지
    if (mode == 0 || span <= 0.0)
        return true;

    // 배치 때와 동일: 창 두께 >= 벽 폭이면 선택창 없이 안쪽 고정, 아니면 위치 옵션을 다시 묻는다
    if (tNew >= span) {
        mode = WallPositionDialog::Inner;
    } else {
        WallPositionDialog dlg(static_cast<WallPositionDialog::Mode>(mode), parent);
        if (dlg.exec() != QDialog::Accepted)
            return false;   // 취소 → 호출측에서 형태 교체 취소
        mode = static_cast<int>(dlg.mode());
    }
    newInfo.props["WallPosMode"] = mode;

    double near1 = 0.0;
    switch (mode) {
    case WallPositionDialog::Inner:  near1 = pNew;                      break;
    case WallPositionDialog::Center: near1 = (tNew - span) / 2.0;       break;
    case WallPositionDialog::Outer:  near1 = (tNew - pNew) - span;      break;
    default: return true;
    }
    const double far1  = near1 + span;
    const double shift = near0 - near1;   // 원점의 로컬 +Y 이동량

    const double angleDeg = oldInfo.props.value("angleDeg").toDouble();
    const bool   flipY    = oldInfo.props.value("flipY").toInt() != 0;
    const double c  = std::cos(qDegreesToRadians(angleDeg));
    const double s2 = std::sin(qDegreesToRadians(angleDeg));
    double ox = oldInfo.props.value("ox").toDouble();
    double oy = oldInfo.props.value("oy").toDouble();
    if (!flipY) { ox -= shift * s2; oy += shift * c; }
    else        { ox += shift * s2; oy -= shift * c; }

    newInfo.props["ox"]         = ox;
    newInfo.props["oy"]         = oy;
    newInfo.props["wallNearLy"] = near1;
    newInfo.props["wallFarLy"]  = far1;
    newInfo.props["trimYLow"]   = flipY ? -far1  : near1;
    newInfo.props["trimYHigh"]  = flipY ? -near1 : far1;
    return true;
}

// ── 창문 형태 치환 (아이콘 클릭 → 창 너비 입력 → 형태 교체) ─────────────────────
void PropertyDock::onSymbolWindowShapeClicked(int shapeIndex)
{
    if (shapeIndex < 0 || shapeIndex > 6)
        return;
    if (m_currentGroupId.isEmpty() || !m_appContext)
        return;

    MyGraphicsScene *scene = nullptr;
    if (!m_selectedItems.isEmpty())
        scene = dynamic_cast<MyGraphicsScene *>(m_selectedItems.first()->scene());
    if (!scene)
        return;

    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(m_currentGroupId))
        return;

    const SymbolInfo oldInfo = reg->symbolInfo(m_currentGroupId);
    if (oldInfo.type != "PlanWindow")
        return;

    const int oldVariant = oldInfo.props.value("Variant", 0).toInt();
    const int oldSashes  = qBound(2, oldInfo.props.value("Sashes", 2).toInt(), 4);

    int newVariant = 0;
    int newSashes  = 2;
    if (shapeIndex <= 2) {
        newSashes  = shapeIndex + 2;
        // 단창/복창은 현재 값을 유지 (상단부·연결형에서 올 때는 창문설정 값)
        newVariant = (oldVariant == 1 || oldVariant == 2) ? oldVariant
                                                         : (PlanWindowTool::sashDouble() ? 2 : 1);
    } else if (shapeIndex <= 5) {
        newSashes  = shapeIndex - 3 + 2;
        newVariant = 3;
    } else {
        newVariant = 4;
        newSashes  = 2;
    }

    const bool sameShape = (newSashes == oldSashes) &&
                           (newVariant == oldVariant ||
                            ((newVariant == 1 || newVariant == 2) && (oldVariant == 1 || oldVariant == 2)));
    if (sameShape)
        return;   // 같은 형태 재클릭

    double curW = oldInfo.props.value("W").toDouble();
    if (curW <= 0.0)
        curW = 1200.0;

    bool ok = false;
    const double w = QInputDialog::getDouble(
        this, tr("창문 형태 변경"), tr("창문 너비 (mm):"), curW, 100.0, 50000.0, 0, &ok);
    if (!ok) {
        // 취소 시 아이콘 체크 상태를 현재 형태로 복원
        int oldIdx = -1;
        if (oldVariant == 1 || oldVariant == 2) oldIdx = oldSashes - 2;
        else if (oldVariant == 3)                oldIdx = 3 + oldSashes - 2;
        else if (oldVariant == 4)                oldIdx = 6;
        QButtonGroup *grp = qobject_cast<QButtonGroup *>(sender());
        if (grp && grp->button(oldIdx))
            grp->button(oldIdx)->setChecked(true);
        return;
    }

    SymbolInfo newInfo = oldInfo;
    newInfo.props["W"]       = w;
    newInfo.props["Variant"] = newVariant;
    newInfo.props["Sashes"]  = newSashes;
    for (int i = 0; i < 7; ++i)
        newInfo.props[QString("Win%1").arg(i + 1)] =
            (newVariant >= 3) ? PlanWindowTool::winParamTop(i) : PlanWindowTool::winParam(i);
    newInfo.props["SashW"] = (newSashes == 4) ? PlanWindowTool::sashWidth4()
                           : (newSashes == 3) ? PlanWindowTool::sashWidth3()
                                              : PlanWindowTool::sashWidth();

    // 새 두께 기준으로 벽 위치 옵션 재적용. V(=배치 시 창 두께)도 새 두께로 갱신
    double tOld = 0.0, pOld = 0.0, tNew = 0.0, pNew = 0.0;
    windowThicknessAndProt(oldInfo.props, tOld, pOld);
    windowThicknessAndProt(newInfo.props, tNew, pNew);
    if (!reapplyWindowWallPosition(oldInfo, newInfo, this)) {
        int oldIdx = -1;
        if (oldVariant == 1 || oldVariant == 2) oldIdx = oldSashes - 2;
        else if (oldVariant == 3)                oldIdx = 3 + oldSashes - 2;
        else if (oldVariant == 4)                oldIdx = 6;
        QButtonGroup *grp2 = qobject_cast<QButtonGroup *>(sender());
        if (grp2 && grp2->button(oldIdx))
            grp2->button(oldIdx)->setChecked(true);
        return;   // 위치 선택 취소 → 형태 교체 취소
    }
    if (qAbs(oldInfo.props.value("V").toDouble() - tOld) < 0.5)
        newInfo.props["V"] = tNew;

    rebuildSymbol(scene, m_currentGroupId, oldInfo, newInfo);
}

// ── 심볼 재빌드 (Undo/Redo 지원) ─────────────────────────────────────────────
void PropertyDock::rebuildSymbol(MyGraphicsScene *scene, const QString &groupId,
                                 const SymbolInfo &oldInfo, const SymbolInfo &newInfo)
{
    if (!m_appContext || !scene)
        return;

    const double ox       = newInfo.props.value("ox").toDouble();
    const double oy       = newInfo.props.value("oy").toDouble();
    const double angleDeg = newInfo.props.value("angleDeg").toDouble();
    const bool   flipY    = newInfo.props.value("flipY").toInt() != 0;
    const double c        = std::cos(qDegreesToRadians(angleDeg));
    const double s2       = std::sin(qDegreesToRadians(angleDeg));
    const QString frameLyr = newInfo.props.value("FrameLayer").toString();
    const QString otherLyr = newInfo.props.value("OtherLayer").toString();

    // ── PlanDoor 전용: 마감선(closing lines) 분리 수집 ────────────────────────
    // PD_DATA_CLOSING_LINE 태그가 있으면 마감선, 없으면 도어 기하학 아이템
    QList<QGraphicsItem *> oldDoorItems;
    QList<QGraphicsItem *> oldClosingLines;
    const QList<QGraphicsItem *> allItems = scene->items();
    for (int i = 0; i < allItems.size(); ++i) {
        QGraphicsItem *it = allItems.at(i);
        if (it->data(PD_DATA_GROUP_ID).toString() != groupId) continue;
        if (!it->data(PD_DATA_CLOSING_LINE).toString().isEmpty())
            oldClosingLines << it;
        else
            oldDoorItems << it;
    }

    // ── 새 도어·창문 기하학 생성 ─────────────────────────────────────────────
    QList<QGraphicsItem *> newItems;
    if (newInfo.type == "PlanDoor") {
        DoorBuildParams p;
        p.ox         = ox;       p.oy        = oy;
        p.angleDeg   = angleDeg; p.flipY     = flipY;
        p.H          = newInfo.props.value("H").toDouble();
        p.V          = newInfo.props.value("V").toDouble();
        p.frameType  = newInfo.props.value("FrameType").toInt();
        p.doorStyle  = newInfo.props.value("DoorStyle").toInt();
        p.frameLayer = frameLyr;
        p.otherLayer = otherLyr;
        newItems = PlanDoorTool::buildGeometry(p, m_appContext);
    } else if (newInfo.type == "PlanDoorDouble") {
        // H1(문1, 시작점 P1측)/H2(문2) 는 onSymbolSpinboxFinished 에서 편집되며,
        // 그때 newInfo.props["H"] = H1+H2 로 함께 갱신되어 이 값이 전체 폭과 일치한다.
        DoorBuildParams p;
        p.ox         = ox;       p.oy        = oy;
        p.angleDeg   = angleDeg; p.flipY     = flipY;
        p.H          = newInfo.props.value("H").toDouble();
        p.h1         = newInfo.props.value("H1").toDouble();
        p.V          = newInfo.props.value("V").toDouble();
        p.frameType  = newInfo.props.value("FrameType").toInt();
        p.doorStyle  = newInfo.props.value("DoorStyle").toInt();
        p.frameLayer = frameLyr;
        p.otherLayer = otherLyr;
        newItems = PlanDoorDoubleTool::buildGeometry(p, m_appContext);
    } else if (newInfo.type == "PlanDoorBiSwing") {
        DoorBuildParams p;
        p.ox         = ox;       p.oy        = oy;
        p.angleDeg   = angleDeg; p.flipY     = flipY;
        p.H          = newInfo.props.value("H").toDouble();
        p.V          = newInfo.props.value("V").toDouble();
        p.frameType  = newInfo.props.value("FrameType").toInt();
        p.frameLayer = frameLyr;
        p.otherLayer = otherLyr;
        newItems = PlanDoorBiSwingTool::buildGeometry(p, m_appContext);
    } else if (newInfo.type == "PlanDoorDoubleBiSwing") {
        DoorBuildParams p;
        p.ox         = ox;       p.oy        = oy;
        p.angleDeg   = angleDeg; p.flipY     = flipY;
        const double h1v = newInfo.props.value("H1").toDouble();
        const double h2v = newInfo.props.value("H2").toDouble();
        p.H          = h1v + h2v;
        p.h1         = h1v;
        p.V          = newInfo.props.value("V").toDouble();
        p.frameType  = newInfo.props.value("FrameType").toInt();
        p.frameLayer = frameLyr;
        p.otherLayer = otherLyr;
        newItems = PlanDoorDoubleBiSwingTool::buildGeometry(p, m_appContext);
    } else if (PlanDoorSimpleTool::isSimpleSymbolType(newInfo.type)) {
        SimpleDoorBuildParams p;
        p.ox         = ox;       p.oy        = oy;
        p.angleDeg   = angleDeg; p.flipY     = flipY;
        p.H          = newInfo.props.value("H").toDouble();
        p.V          = newInfo.props.value("V").toDouble();
        p.layer      = frameLyr;
        p.arcLayer   = otherLyr;
        p.kind       = PlanDoorSimpleTool::kindForSymbolType(newInfo.type);
        newItems = PlanDoorSimpleTool::buildGeometry(p, m_appContext);
    } else if (newInfo.type == "PlanRevolvingDoor") {
        RevolvingDoorBuildParams p;
        p.ox         = ox;       p.oy        = oy;
        p.angleDeg   = angleDeg; p.flipY     = flipY;
        p.H          = newInfo.props.value("H").toDouble();
        p.V          = newInfo.props.value("V").toDouble();
        p.wingCount  = newInfo.props.value("WingCount", 4).toInt() == 3 ? 3 : 4;
        p.frameLayer = frameLyr;
        p.otherLayer = otherLyr;
        newItems = PlanRevolvingDoorTool::buildGeometry(p, m_appContext);
    } else if (newInfo.type == "PlanWindow") {
        WindowBuildParams p;
        p.ox         = ox;       p.oy        = oy;
        p.angleDeg   = angleDeg; p.flipY     = flipY;
        p.W          = newInfo.props.value("W").toDouble();
        p.V          = newInfo.props.value("V").toDouble();
        p.D1         = newInfo.props.value("D1").toDouble();
        p.D2         = newInfo.props.value("D2").toDouble();
        p.variant    = newInfo.props.value("Variant", 0).toInt();
        p.glass      = newInfo.props.value("Glass", 0).toInt() != 0;
        p.sashW      = newInfo.props.value("SashW", 0.0).toDouble();
        p.sashes     = qBound(2, newInfo.props.value("Sashes", 2).toInt(), 4);
        for (int wi = 0; wi < 7; ++wi) {
            const double wv = newInfo.props.value(QString("Win%1").arg(wi + 1), p.win[wi]).toDouble();
            if (wv > 0.0) p.win[wi] = wv;
        }
        p.frameLayer = frameLyr;
        p.otherLayer = otherLyr;
        newItems = PlanWindowTool::buildGeometry(p, m_appContext);
    }
    if (newItems.isEmpty())
        return;

    for (int i = 0; i < newItems.size(); ++i)
        newItems.at(i)->setData(PD_DATA_GROUP_ID, groupId);

    // ── Undo 매크로 시작 ─────────────────────────────────────────────────────
    QUndoStack *stack = m_appContext->undoStack();
    stack->beginMacro(tr("심볼 속성 변경"));

    // 1. 마감선 제거
    for (int i = 0; i < oldClosingLines.size(); ++i)
        stack->push(new RemoveEntityCommand(scene, oldClosingLines.at(i)));

    // 2. 벽 래치쪽 끝점 조정 (도어류이고 벽 트림이 있었던 경우만)
    // adjustLatchWallEndpoints 는 문짝 형태와 무관한 순수 벽-트림 로직이므로
    // PlanDoorTool 의 정적 함수를 쌍여닫이·양방향여닫이·양방쌍여닫이에도 그대로 재사용한다.
    if (newInfo.type == "PlanDoor" || newInfo.type == "PlanDoorDouble" ||
        newInfo.type == "PlanDoorBiSwing" || newInfo.type == "PlanDoorDoubleBiSwing" ||
        newInfo.type == "PlanRevolvingDoor" || PlanDoorSimpleTool::isSimpleSymbolType(newInfo.type) ||
        newInfo.type == "PlanWindow") {
        const QString wallGroupId = newInfo.props.value("wallGroupId").toString();
        if (!wallGroupId.isEmpty()) {
            // 도어는 H, 창문은 W 가 개구부 너비
            const char *wKey      = (newInfo.type == "PlanWindow") ? "W" : "H";
            const double H_old    = oldInfo.props.value(wKey).toDouble();
            const double H_new    = newInfo.props.value(wKey).toDouble();
            const double trimYLow  = newInfo.props.value("trimYLow").toDouble();
            const double trimYHigh = newInfo.props.value("trimYHigh").toDouble();
            PlanDoorTool::adjustLatchWallEndpoints(
                scene, wallGroupId,
                ox, oy, c, s2,
                H_old, H_new,
                trimYLow, trimYHigh,
                stack,
                newInfo.props.value("SpaceExtR").toDouble());
        }
    }

    // 3. 도어 기하학 제거 → 레지스트리 갱신 → 신규 도어 추가
    for (int i = 0; i < oldDoorItems.size(); ++i)
        stack->push(new RemoveEntityCommand(scene, oldDoorItems.at(i)));
    stack->push(new UpdateRegistryCommand(scene->symbolRegistry(), groupId, oldInfo, newInfo));
    for (int i = 0; i < newItems.size(); ++i)
        stack->push(new AddEntityCommand(scene, newItems.at(i)));

    // 4-1. 단순문 마감선 (P3 레이어 = props["ClosingLayer"])
    if (PlanDoorSimpleTool::isSimpleSymbolType(newInfo.type)) {
        const QString wallGroupId = newInfo.props.value("wallGroupId").toString();
        if (!wallGroupId.isEmpty()) {
            QString lyr = newInfo.props.value("ClosingLayer").toString();
            if (lyr.isEmpty()) lyr = frameLyr;
            PlanDoorSimpleTool::addClosingLinesStatic(
                scene, m_appContext, ox, oy, c, s2, flipY,
                newInfo.props.value("H").toDouble(),
                newInfo.props.value("V").toDouble(),
                lyr, groupId, stack);
            // 공간벽 마감형태 연결선 재생성
            SpaceFinish::addConnectorsFromProps(
                scene, m_appContext, ox, oy, c, s2,
                newInfo.props.value("H").toDouble(),
                newInfo.props, groupId, stack);
        }
    }

    // 4-2. 창문: 공간벽 마감형태 연결선 재생성
    if (newInfo.type == "PlanWindow") {
        const QString wallGroupId = newInfo.props.value("wallGroupId").toString();
        if (!wallGroupId.isEmpty()) {
            SpaceFinish::addConnectorsFromProps(
                scene, m_appContext, ox, oy, c, s2,
                newInfo.props.value("W").toDouble(),
                newInfo.props, groupId, stack);
        }

        // 절단된 벽선 끝점 마감선 재생성 — 벽 위치 옵션이 적용된 창(또는 기존 마감선이 있던 창)만.
        // 다중선 유무와 무관하게 처리 (배치 시와 동일 조건).
        // 창문은 벽이 창틀보다 안/바깥으로 나온 쪽만 마감하므로 반대쪽은 0/V 로 치환
        if (!oldClosingLines.isEmpty() || newInfo.props.value("WallPosMode", 0).toInt() != 0) {
            const double V      = newInfo.props.value("V").toDouble();
            const double nearLy = newInfo.props.value("wallNearLy").toDouble();
            const double farLy  = newInfo.props.value("wallFarLy").toDouble();
            const double EPS    = 1e-4;
            QString closeLyr = newInfo.props.value("ClosingLayer").toString();
            if (!oldClosingLines.isEmpty()) {
                const CadItem *oldCl = dynamic_cast<const CadItem *>(oldClosingLines.first());
                if (oldCl && !oldCl->layerName().isEmpty())
                    closeLyr = oldCl->layerName();
            }
            if (closeLyr.isEmpty())
                closeLyr = otherLyr;
            PlanDoorTool::addClosingLinesStatic(
                scene, m_appContext, ox, oy, c, s2, flipY,
                newInfo.props.value("W").toDouble(), V,
                (nearLy < -EPS) ? nearLy : 0.0,
                (farLy > V + EPS) ? farLy : V,
                closeLyr, groupId, stack);
        }
    }

    // 4. 새 마감선 추가 (도어류이고 벽 트림이 있었던 경우만)
    if (newInfo.type == "PlanDoor" || newInfo.type == "PlanDoorDouble" ||
        newInfo.type == "PlanDoorBiSwing" || newInfo.type == "PlanDoorDoubleBiSwing" ||
        newInfo.type == "PlanRevolvingDoor") {
        const QString wallGroupId = newInfo.props.value("wallGroupId").toString();
        if (!wallGroupId.isEmpty()) {
            const double wallNearLy = newInfo.props.value("wallNearLy").toDouble();
            const double wallFarLy  = newInfo.props.value("wallFarLy").toDouble();
            const double H_new = newInfo.props.value("H").toDouble();
            const double V     = newInfo.props.value("V").toDouble();
            PlanDoorTool::addClosingLinesStatic(
                scene, m_appContext,
                ox, oy, c, s2, flipY,
                H_new, V,
                wallNearLy, wallFarLy,
                otherLyr, groupId, stack);

            // 공간벽 마감형태(swtype2~4) 연결선 재생성 — 새 문 너비 기준 (PlanDoor 전용 props)
            SpaceFinish::addConnectorsFromProps(
                scene, m_appContext,
                ox, oy, c, s2, H_new,
                newInfo.props,
                groupId, stack);
        }
    }

    stack->endMacro();
}

// ── 심볼 삭제 (벽 복원 포함) ─────────────────────────────────────────────────
void PropertyDock::onDeleteSymbol()
{
    if (m_currentGroupId.isEmpty() || !m_appContext)
        return;

    MyGraphicsScene *scene = nullptr;
    if (!m_selectedItems.isEmpty())
        scene = dynamic_cast<MyGraphicsScene *>(m_selectedItems.first()->scene());
    if (!scene)
        return;

    SceneSymbolRegistry *reg = scene->symbolRegistry();
    if (!reg->hasSymbol(m_currentGroupId))
        return;

    const SymbolInfo info    = reg->symbolInfo(m_currentGroupId);
    const QString    groupId = m_currentGroupId;

    // 도어/창문 아이템과 마감선을 분리 수집
    QList<QGraphicsItem *> doorItems, closingLines;
    const QList<QGraphicsItem *> allItems = scene->items();
    for (int i = 0; i < allItems.size(); ++i) {
        QGraphicsItem *it = allItems.at(i);
        if (it->data(PD_DATA_GROUP_ID).toString() != groupId) continue;
        if (!it->data(PD_DATA_CLOSING_LINE).toString().isEmpty())
            closingLines << it;
        else
            doorItems << it;
    }

    QUndoStack *stack = m_appContext->undoStack();
    stack->beginMacro(tr("심볼 삭제"));

    // 1. 마감선 제거
    for (int i = 0; i < closingLines.size(); ++i)
        stack->push(new RemoveEntityCommand(scene, closingLines.at(i)));

    // 2. 벽 복원 (wallGroupId 가 있는 도어/창문)
    const QString wallGroupId = info.props.value("wallGroupId").toString();
    if (!wallGroupId.isEmpty()) {
        const double ox        = info.props.value("ox").toDouble();
        const double oy        = info.props.value("oy").toDouble();
        const double angleDeg  = info.props.value("angleDeg").toDouble();
        const double c         = std::cos(qDegreesToRadians(angleDeg));
        const double s2        = std::sin(qDegreesToRadians(angleDeg));
        const double trimYLow  = info.props.value("trimYLow").toDouble();
        const double trimYHigh = info.props.value("trimYHigh").toDouble();
        // 도어(외여닫이·쌍여닫이·양방향여닫이·양방쌍여닫이)는 H, 창문은 W 가 개구부 너비
        const bool isDoorType = info.type == "PlanDoor" || info.type == "PlanDoorDouble" ||
                               info.type == "PlanDoorBiSwing" || info.type == "PlanDoorDoubleBiSwing" ||
                               info.type == "PlanRevolvingDoor" || PlanDoorSimpleTool::isSimpleSymbolType(info.type);
        const double openingW  = isDoorType
                                 ? info.props.value("H").toDouble()
                                 : info.props.value("W").toDouble();

        PlanDoorTool::restoreWallOpening(
            scene, wallGroupId,
            ox, oy, c, s2,
            openingW, trimYLow, trimYHigh,
            stack,
            info.props.value("SpaceExtL").toDouble(),
            info.props.value("SpaceExtR").toDouble());
    }

    // 3. 도어 기하학 아이템 제거
    for (int i = 0; i < doorItems.size(); ++i)
        stack->push(new RemoveEntityCommand(scene, doorItems.at(i)));

    // 4. 레지스트리에서 제거
    stack->push(new UnregisterSymbolCommand(reg, groupId, info));

    stack->endMacro();

    // 도크 초기화
    m_currentGroupId.clear();
    m_selectedItems.clear();
    clearTables();
}

QString PropertyDock::translateKey(const QString &key) {
    if (key == "Type") return tr("유형");
    if (key == "Layer") return tr("도면층");
    if (key == "Prefix") return tr("머릿말기호");
    if (key == "Content") return tr("내용");
    if (key == "FontFamily") return tr("글꼴");
    if (key == "FontSize") return tr("글꼴 크기");
    if (key == "Rotation") return tr("회전");
    if (key == "Suffix") return tr("꼬리말");
    if (key == "Precision") return tr("소수점 자리");
    if (key == "TolType") return tr("공차유형");
    if (key == "TolUpper") return tr("상한공차,대칭");
    if (key == "TolLower") return tr("하한공차");
    if (key == "TolPrecision") return tr("공차정밀도");
    if (key == "FitCode") return tr("끼워맞춤기호");
    if (key == "FitShowValues") return tr("공차값 병기");
    if (key == "SuppressZero") return tr("주치수 제로 억제");
    if (key == "SuppressTolZero") return tr("공차 제로 억제");
    if (key == "ThousandSep") return tr("자릿점 표시");
    if (key == "HideExt1") return tr("보조선숨기기1");
    if (key == "HideExt2") return tr("보조선숨기기2");
    if (key == "DimType") return "DimType";
    if (key == "Length") return tr("길이");
    if (key == "MLWidth") return tr("ML 전체 폭");
    if (key == "Radius") return tr("반지름");
    if (key == "Scale") return tr("축척");
    if (key == "StartPoint") return tr("시작점");
    if (key == "EndPoint") return tr("끝점");
    if (key == "CenterPoint") return tr("중심점");
    if (key == "StartAngle") return "StartAngle";
    if (key == "SpanAngle") return "SpanAngle";
    if (key == "RaValue") return tr("Ra 값");
    if (key == "ProductionMethod") return tr("가공방법");
    if (key == "CutoffValue") return tr("컷오프값");
    if (key == "LaySymbol") return tr("줄무늬 방향");
    if (key == "MachiningAllowance") return tr("가공 여유");
    if (key == "LegacyGrade") return tr("다듬질 등급");
    if (key == "ShowTailBar") return tr("꼬리선");
    if (key == "RaLetterCode") return tr("등급 문자(W/X/Y/Z)");
    if (key == "SymbolLayer") return tr("기호레이어");
    if (key == "TextLayer") return tr("문자레이어");
    if (key == "TextHeight") return tr("문자 높이");
    if (key == "CenterX") return tr("중심 X");
    if (key == "CenterY") return tr("중심 Y");
    if (key == "Diameter") return tr("지름");
    if (key == "ThroughHole") return tr("관통 여부");
    if (key == "Depth") return tr("깊이");
    if (key == "IsThreaded") return tr("탭가공");
    if (key == "ThreadSpec") return tr("나사 규격");
    if (key == "ThreadClass") return tr("나사 등급");
    if (key == "ThreadDepth") return tr("나사 깊이");
    if (key == "ToleranceClass") return tr("공차 등급");
    if (key == "HasCounterbore") return tr("카운터보어 가공");
    if (key == "CbDiameter") return tr("카운터보어 지름");
    if (key == "CbDepth") return tr("카운터보어 깊이");
    if (key == "HasCountersink") return tr("카운터싱크가공");
    if (key == "CsDiameter") return tr("카운터싱크 지름");
    if (key == "CsAngle") return tr("카운터싱크 각도");
    if (key == "SymbolStyle") return tr("기호 표기 방식");
    if (key == "BaseLayer") return tr("기본 레이어");
    if (key == "ThreadLayer") return tr("나사 레이어");
    if (key == "CbLayer") return tr("카운터보어 레이어");
    if (key == "CsLayer") return tr("카운터싱크 레이어");
    return key;
}
