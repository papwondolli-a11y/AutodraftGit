#ifndef TOPTOOLBAR_H
#define TOPTOOLBAR_H

#include "layer.h"
#include "layertableview.h"
#include "clickablecombobox.h"

#include <QActionGroup>
#include <QFontComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QString>
#include <QToolBar>
#include <QToolButton>

class topToolBar : public QMainWindow {
    Q_OBJECT
public:
    explicit topToolBar(AppContext *context, QWidget *parent = nullptr);
    QList<Layer> layers;

    void updateLayerModel();
    void connectComboBox(LayerTableView *m_table);
    QToolBar *toolBarsnap() const { return snapToolBar; }
    QToolBar *toolBarfont() const { return fontToolBar; }
    QToolBar *toolBarlayer() const { return layerToolBar; }

private slots:
    void onActionTriggered(bool checked);
    void onfontComboTriggered(const QFont &newFont);
    void FontSizeChanged(const QString &newSize);
    void onFontStyleButtonToggled(bool checked);
    void onSizeComboCommitted();
    void currentLayerView();
    void textLayerNameChanged(const QString &layerName);

public slots:
    void updateComboBox();
    void onNameSelected(const QString &name);
    void onFontChanged(const QString &family, qreal size);
    void onFontStyleSync(bool bold, bool italic, bool underline);
    void onSelectedTextFont(const QString &family, qreal size);
    void onLayerChanged(const QString &layerName);
    void onTextLayerChanged(const QString &layerName);

signals:
    void tableCurrentCheck(const QString &nameToCheck, int col, QString icon1,
                           QString icon2);

private:
    QToolBar *snapToolBar = nullptr;
    QToolBar *fontToolBar = nullptr;
    QToolBar *layerToolBar = nullptr;
    QActionGroup *snapToolGroup = nullptr;
    QActionGroup *layerToolGroup = nullptr;
    QActionGroup *fontToolGroup = nullptr;
    QFontComboBox *fontCombo = nullptr;
    QComboBox *sizeCombo = nullptr;
    QToolButton *boldButton = nullptr;
    QToolButton *italicButton = nullptr;
    QToolButton *underlineButton = nullptr;

    QLabel *currentLayer = nullptr;
    QLabel *currentLineType = nullptr;
    QLabel *currentWeight = nullptr;
    AppContext *m_appContext = nullptr;

    ClickableComboBox *textLayerCombo = nullptr;
    ClickableComboBox *layerToolCombo = nullptr;

    QAction *findSnapAction(const QString &name);
    void createSnapToolButton();
    void applyPropertyToSelectedText(const QString &key, const QVariant &value);
    void returnFocusToActiveView();

};

#endif // TOPTOOLBAR_H