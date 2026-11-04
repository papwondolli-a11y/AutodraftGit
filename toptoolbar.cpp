#include "toptoolbar.h"
#include "appcontext.h"
#include "layermanager.h"
#include "mygraphicsview.h"
#include "mygraphicsscene.h"
#include "cadcommands.h"
#include "textitem.h"
#include <QUndoStack>

#include <QDebug>
#include <QDoubleValidator>
#include <QLabel>
#include <QLineEdit>
#include <QToolButton>

topToolBar::topToolBar(AppContext *context, QWidget *parent)
    : m_appContext(context) {
    Q_UNUSED(parent)

    // ----------------------------------------------------
    // text font select toolbar
    // ----------------------------------------------------
    fontToolBar = addToolBar("FontToolBar");
    fontToolBar->setObjectName("TopFontToolBar"); // 스타일 타겟팅용
    fontToolGroup = new QActionGroup(fontToolBar);

    QLabel *fontLabel = new QLabel(tr("  글꼴  :  "), fontToolBar);
    fontCombo = new QFontComboBox(fontToolBar);
    fontCombo->setObjectName("TopFontCombo");

    sizeCombo = new QComboBox(fontToolBar);
    sizeCombo->setObjectName("TopSizeCombo");
    fontCombo->setMinimumWidth(100);
    fontCombo->setMaximumWidth(120);
    fontCombo->setMaximumHeight(30);

    if (fontCombo->lineEdit()) {
        fontCombo->lineEdit()->setProperty("noRedirect", true);
    }
    fontCombo->setProperty("noRedirect", true);

    sizeCombo->setMinimumWidth(60);
    sizeCombo->setMaximumWidth(80);
    sizeCombo->setMaximumHeight(33);
    sizeCombo->setEditable(true);

    if (sizeCombo->lineEdit()) {
        sizeCombo->lineEdit()->setAlignment(Qt::AlignCenter);
        sizeCombo->lineEdit()->setProperty("noRedirect", true);
    }
    sizeCombo->setProperty("noRedirect", true);

    QDoubleValidator *validator = new QDoubleValidator(0.1, 500.0, 1, sizeCombo);
    validator->setNotation(QDoubleValidator::StandardNotation);
    sizeCombo->setValidator(validator);

    // C++11 range-loop 대신 인덱스 기반 루프 사용
    for (int i = 0; i <= 22; ++i) {
        double value = 1.0 + (i * 0.5);
        sizeCombo->addItem(QString::number(value, 'f', 1));
    }

    // ----------------------------------------------------
    // 텍스트 전용 레이어 콤보박스 추가
    // ----------------------------------------------------
    QLabel *textLayerLabel = new QLabel(tr(" 글자 레이어 : "), fontToolBar);
    textLayerCombo = new ClickableComboBox(fontToolBar);
    textLayerCombo->setObjectName("TopTextLayerCombo");
    textLayerCombo->setMinimumWidth(100);
    textLayerCombo->setMaximumWidth(150);
    textLayerCombo->setMaximumHeight(33);
    textLayerCombo->setEditable(true);

    if (textLayerCombo->lineEdit()) {
        textLayerCombo->lineEdit()->setReadOnly(true);
        textLayerCombo->lineEdit()->setAlignment(Qt::AlignCenter);
        textLayerCombo->lineEdit()->setProperty("noRedirect", true);
    }
    textLayerCombo->setProperty("noRedirect", true);

    fontToolBar->addWidget(fontLabel);
    fontToolBar->addWidget(fontCombo);
    fontToolBar->addWidget(sizeCombo);

    // 굵게 / 기울임 / 밑줄 토글 버튼 (Word 와 같은 방식)
    boldButton = new QToolButton(fontToolBar);
    italicButton = new QToolButton(fontToolBar);
    underlineButton = new QToolButton(fontToolBar);
    boldButton->setObjectName("TopFontBoldButton");
    italicButton->setObjectName("TopFontItalicButton");
    underlineButton->setObjectName("TopFontUnderlineButton");
    boldButton->setText("B");
    italicButton->setText("I");
    underlineButton->setText("U");
    boldButton->setToolTip(tr("굵게"));
    italicButton->setToolTip(tr("기울임"));
    underlineButton->setToolTip(tr("밑줄"));
    QToolButton *styleButtons[3] = {boldButton, italicButton, underlineButton};
    for (int i = 0; i < 3; ++i) {
        QFont bf = styleButtons[i]->font();
        bf.setBold(i == 0);
        bf.setItalic(i == 1);
        bf.setUnderline(i == 2);
        styleButtons[i]->setFont(bf);
        styleButtons[i]->setCheckable(true);
        styleButtons[i]->setAutoRaise(true);
        styleButtons[i]->setFixedSize(28, 28);
        styleButtons[i]->setFocusPolicy(Qt::NoFocus);
        styleButtons[i]->setProperty("noRedirect", true);
        fontToolBar->addWidget(styleButtons[i]);
    }
    fontToolBar->addWidget(textLayerLabel);
    fontToolBar->addWidget(textLayerCombo);

    // ----------------------------------------------------
    // current layer view
    // ----------------------------------------------------
    layerToolBar = addToolBar("layerToolBar");
    layerToolBar->setObjectName("TopLayerToolBar");
    layerToolGroup = new QActionGroup(layerToolBar);

    currentLayer = new QLabel(tr(" 현재 레이어  : "), layerToolBar);
    layerToolCombo = new ClickableComboBox(layerToolBar);
    layerToolCombo->setObjectName("TopLayerToolCombo");
    layerToolCombo->setMinimumWidth(100);
    layerToolCombo->setMaximumWidth(150);
    layerToolCombo->setMaximumHeight(33);
    layerToolCombo->setEditable(true);

    if (layerToolCombo->lineEdit()) {
        layerToolCombo->lineEdit()->setReadOnly(true);
        layerToolCombo->lineEdit()->setAlignment(Qt::AlignCenter);
        layerToolCombo->lineEdit()->setProperty("noRedirect", true);
    }

    currentLineType = new QLabel(tr("현재레이어"), layerToolBar);
    currentWeight = new QLabel(tr("현재레이어"), layerToolBar);

    updateComboBox();

    layerToolBar->addWidget(currentLayer);
    layerToolBar->addWidget(layerToolCombo);
    layerToolBar->addWidget(currentLineType);
    layerToolBar->addWidget(currentWeight);

    // ----------------------------------------------------
    // Signal - Slot 연결 (람다 대신 포인터 함수 사용)
    // ----------------------------------------------------
    connect(fontCombo, &QFontComboBox::currentFontChanged, this,
            &topToolBar::onfontComboTriggered);
    connect(sizeCombo, &QComboBox::currentTextChanged, this,
            &topToolBar::FontSizeChanged);
    connect(boldButton, &QToolButton::toggled, this,
            &topToolBar::onFontStyleButtonToggled);
    connect(italicButton, &QToolButton::toggled, this,
            &topToolBar::onFontStyleButtonToggled);
    connect(underlineButton, &QToolButton::toggled, this,
            &topToolBar::onFontStyleButtonToggled);
    connect(sizeCombo, QOverload<int>::of(&QComboBox::activated), this,
            &topToolBar::onSizeComboCommitted);
    if (sizeCombo->lineEdit()) {
        connect(sizeCombo->lineEdit(), &QLineEdit::returnPressed, this,
                &topToolBar::onSizeComboCommitted);
    }
    connect(layerToolCombo, QOverload<int>::of(&QComboBox::activated), this,
            &topToolBar::currentLayerView);
    connect(textLayerCombo, &QComboBox::currentTextChanged, this,
            &topToolBar::textLayerNameChanged);

    if (m_appContext) {
        connect(m_appContext, &AppContext::currentFontChanged, this,
                &topToolBar::onFontChanged);
        connect(m_appContext, &AppContext::currentFontStyleChanged, this,
                &topToolBar::onFontStyleSync);
        connect(m_appContext, &AppContext::selectedTextStyleChanged, this,
                &topToolBar::onFontStyleSync);
        connect(m_appContext, &AppContext::selectedTextFontChanged, this,
                &topToolBar::onSelectedTextFont);
        connect(m_appContext, &AppContext::currentLayerChanged, this,
                &topToolBar::onLayerChanged);
        connect(m_appContext, &AppContext::textLayerChanged, this,
                &topToolBar::onTextLayerChanged);

        // [추가] 초기 폰트 및 사이즈 설정 반영
        onFontChanged(m_appContext->currentFontFamily(), m_appContext->currentFontSize());
    }
}

void topToolBar::currentLayerView() {
    returnFocusToActiveView();
}

void topToolBar::onfontComboTriggered(const QFont &newFont) {
    if (m_appContext) {
        m_appContext->setCurrentFontFamily(newFont.family());
        applyPropertyToSelectedText("FontFamily", newFont.family());
    }
    returnFocusToActiveView();
}

void topToolBar::FontSizeChanged(const QString &newSize) {
    if (m_appContext) {
        bool ok;
        double size = newSize.toDouble(&ok);
        if (ok) {
            m_appContext->setCurrentFontSize(size);
        }
    }
    returnFocusToActiveView();
}

void topToolBar::onFontStyleButtonToggled(bool checked) {
    QToolButton *btn = qobject_cast<QToolButton *>(sender());
    if (!m_appContext || !btn)
        return;

    // 1. 새로 쓸 글자의 현재 서식
    m_appContext->setCurrentFontStyle(boldButton->isChecked(),
                                      italicButton->isChecked(),
                                      underlineButton->isChecked());

    // 2. 선택된 기존 글자에 바로 적용 (한 번의 Undo 로 되돌림)
    const char *key = nullptr;
    if (btn == boldButton)           key = "FontBold";
    else if (btn == italicButton)    key = "FontItalic";
    else if (btn == underlineButton) key = "FontUnderline";

    if (key)
        applyPropertyToSelectedText(key, checked);
    returnFocusToActiveView();
}

// 선택된 글자(TextItem)들 중 값이 다른 것만 골라 한 번의 Undo 로 속성을 바꾼다.
void topToolBar::applyPropertyToSelectedText(const QString &key,
                                             const QVariant &value) {
    if (!m_appContext)
        return;
    MyGraphicsScene *scene = m_appContext->activeScene();
    QUndoStack *stack = m_appContext->undoStack();
    if (!scene || !stack)
        return;

    QList<TextItem *> targets;
    QList<QVariant> oldValues;
    const QList<QGraphicsItem *> selected = scene->selectedItems();
    for (int i = 0; i < selected.size(); ++i) {
        TextItem *ti = dynamic_cast<TextItem *>(selected.at(i));
        if (!ti)
            continue;
        const QVariant old = ti->getProperties().value(key);
        const bool same = (value.typeId() == QMetaType::Double)
                              ? qFuzzyCompare(old.toDouble() + 1.0, value.toDouble() + 1.0)
                              : old == value;
        if (!same) {
            targets.append(ti);
            oldValues.append(old);
        }
    }
    if (targets.isEmpty())
        return;

    stack->beginMacro(tr("글자 서식 변경"));
    for (int i = 0; i < targets.size(); ++i)
        stack->push(new ChangePropertyCommand(targets.at(i), key, oldValues.at(i), value));
    stack->endMacro();
}

// 크기 콤보: 목록 선택이나 Enter 로 확정됐을 때만 선택된 글자에 적용한다
// (입력 도중 "2", "2." 같은 중간값이 글자에 적용되지 않도록).
void topToolBar::onSizeComboCommitted() {
    bool ok = false;
    const double size = sizeCombo->currentText().toDouble(&ok);
    if (!ok || size <= 0.0 || !m_appContext)
        return;
    MyGraphicsScene *scene = m_appContext->activeScene();
    const double drawScale = (scene && scene->drawingScale() > 0.0) ? scene->drawingScale() : 1.0;
    applyPropertyToSelectedText("FontSize", size * drawScale);
    returnFocusToActiveView();
}

// 글자를 선택했을 때 글꼴/크기 콤보에 그 글자의 값을 보여준다(새 글자의 현재 글꼴은 그대로).
void topToolBar::onSelectedTextFont(const QString &family, qreal size) {
    if (!family.isEmpty()) {
        fontCombo->blockSignals(true);
        fontCombo->setCurrentFont(QFont(family));
        fontCombo->blockSignals(false);
    }
    if (size > 0.0) {
        QString sizeText = QString::number(size, 'f', 1);
        if (sizeText.endsWith(".0"))
            sizeText = QString::number(size, 'f', 0);
        sizeCombo->blockSignals(true);
        const int index = sizeCombo->findText(sizeText);
        if (index >= 0)
            sizeCombo->setCurrentIndex(index);
        else
            sizeCombo->setEditText(sizeText);
        sizeCombo->blockSignals(false);
    }
}

void topToolBar::onFontStyleSync(bool bold, bool italic, bool underline) {
    QToolButton *buttons[3] = {boldButton, italicButton, underlineButton};
    const bool states[3] = {bold, italic, underline};
    for (int i = 0; i < 3; ++i) {
        if (!buttons[i] || buttons[i]->isChecked() == states[i])
            continue;
        buttons[i]->blockSignals(true);
        buttons[i]->setChecked(states[i]);
        buttons[i]->blockSignals(false);
    }
}

void topToolBar::textLayerNameChanged(const QString &layerName) {
    if (m_appContext) {
        m_appContext->setTextLayerName(layerName);
    }
    returnFocusToActiveView();
}

void topToolBar::onActionTriggered(bool checked) {
    if (!snapToolGroup) return;

    QAction *action = qobject_cast<QAction *>(sender());
    if (action) {
        QList<QAction *> actions = snapToolGroup->actions();
        if (action->text() == "osnapNon" && checked) {
            for (int i = 0; i < actions.size(); ++i) {
                QAction *otherAction = actions.at(i);
                if (otherAction != action) {
                    otherAction->setChecked(false);
                }
            }
        } else if (action->text() != "osnapNon" && action->isChecked()) {
            QAction *nonAction = findSnapAction("osnapNon");
            if (nonAction) {
                nonAction->setChecked(false);
            }
        }
//        qDebug() << action->text() << "is activated";
    }
}

QAction *topToolBar::findSnapAction(const QString &name) {
    if (!snapToolGroup) return nullptr;

    QList<QAction *> actions = snapToolGroup->actions();
    for (int i = 0; i < actions.size(); ++i) {
        QAction *action = actions.at(i);
        if (action->text() == name) {
            return action;
        }
    }
    return nullptr;
}

void topToolBar::connectComboBox(LayerTableView *m_table) {
    connect(m_table, &LayerTableView::dataChangedExternally, this,
            &topToolBar::updateComboBox);
    connect(layerToolCombo, &QComboBox::currentTextChanged, this,
            &topToolBar::onNameSelected);
}

void topToolBar::updateComboBox() {
    layerToolCombo->blockSignals(true);
    textLayerCombo->blockSignals(true);
    layerToolCombo->clear();
    textLayerCombo->clear();

    int checkindex = -1;
    int textLayerIndex = -1;

    layers = m_appContext->layerManager()->getAllLayers();
    QString currentTextLayer = m_appContext->textLayerName();

    for (int i = 0; i < layers.size(); ++i) {
        const Layer &layer = layers.at(i);
        QString layerName = layer.name();
        QColor layerColor = layer.color();
        QPixmap pixmap(20, 20);
        pixmap.fill(layerColor);
        QIcon icon(pixmap);

        layerToolCombo->addItem(icon, layerName);
        layerToolCombo->setItemData(layerToolCombo->count() - 1, Qt::AlignCenter,
                                    Qt::TextAlignmentRole);

        textLayerCombo->addItem(icon, layerName);
        textLayerCombo->setItemData(textLayerCombo->count() - 1, Qt::AlignCenter,
                                    Qt::TextAlignmentRole);

        bool iscurrent = layer.isCurrent();
        if (iscurrent) {
            currentLineType->setText(tr("   선 종류 : ") + layer.linetype());
            QString weight = QString::number(layer.weight(), 'f', 2);
            currentWeight->setText(tr("   선 두께 : ") + weight + " mm");
            checkindex = i;
        }

        if (layerName == currentTextLayer) {
            textLayerIndex = i;
        }
    }

    if (checkindex >= 0)
        layerToolCombo->setCurrentIndex(checkindex);

    if (textLayerIndex >= 0) {
        textLayerCombo->setCurrentIndex(textLayerIndex);
    } else if (textLayerCombo->count() > 0) {
        textLayerCombo->setCurrentIndex(0);
        textLayerNameChanged(textLayerCombo->currentText());
    }

    layerToolCombo->blockSignals(false);
    textLayerCombo->blockSignals(false);
}

void topToolBar::onNameSelected(const QString &layerName) {
    for (int i = 0; i < layers.size(); ++i) {
        const Layer &layer = layers.at(i);
        if (layer.name() == layerName) {
            currentLineType->setText(tr("   선 종류 : ") + layer.linetype());
            QString weight = QString::number(layer.weight(), 'f', 2);
            currentWeight->setText(tr("   선 두께 : ") + weight + " mm");
            break;
        }
    }
    emit tableCurrentCheck(layerName, 0, "icon1", "icon2");
    returnFocusToActiveView();
}

void topToolBar::returnFocusToActiveView() {
    if (m_appContext && m_appContext->activeView()) {
        m_appContext->activeView()->setFocus();
    }
}

void topToolBar::onFontChanged(const QString &family, qreal size) {
    fontCombo->blockSignals(true);
    sizeCombo->blockSignals(true);

    fontCombo->setCurrentFont(QFont(family));

    QString sizeText = QString::number(size, 'f', 1);
    if (sizeText.endsWith(".0")) {
        sizeText = QString::number(size, 'f', 0);
    }

    int index = sizeCombo->findText(sizeText);
    if (index >= 0) {
        sizeCombo->setCurrentIndex(index);
    } else {
        sizeCombo->setEditText(sizeText);
    }

    fontCombo->blockSignals(false);
    sizeCombo->blockSignals(false);
}

void topToolBar::onLayerChanged(const QString &layerName) {
    Q_UNUSED(layerName);
    updateComboBox();
}

void topToolBar::onTextLayerChanged(const QString &layerName) {
    textLayerCombo->blockSignals(true);
    int index = textLayerCombo->findText(layerName);
    if (index >= 0) {
        textLayerCombo->setCurrentIndex(index);
    }
    textLayerCombo->blockSignals(false);
}

void topToolBar::updateLayerModel() {
    if (!m_appContext)
        return;

    auto mgr = m_appContext->layerManager();
    if (mgr) {
        disconnect(mgr, &LayerManager::currentLayerChanged, this,
                   &topToolBar::onLayerChanged);
        connect(mgr, &LayerManager::currentLayerChanged, this,
                &topToolBar::onLayerChanged);
    }
    updateComboBox();
}