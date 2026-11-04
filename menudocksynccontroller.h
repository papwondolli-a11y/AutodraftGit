#ifndef MENUDOCKSYNCCONTROLLER_H
#define MENUDOCKSYNCCONTROLLER_H

#include <QObject>

class CustomMenuBar;
class CommandDock;
class AppContext;
class QDockWidget;

class MenuDockSyncController : public QObject {
    Q_OBJECT
public:
    explicit MenuDockSyncController(CustomMenuBar *menuBar, CommandDock *commandDock, 
                                   QDockWidget *lineTypeDock, QDockWidget *layerTableDock,
                                   QDockWidget *blockPaletteDock, QDockWidget *propertyDock,
                                   AppContext *appContext, QObject *parent = nullptr);

private slots:
    void onCommandLineToggled(bool checked);
    void onCommandDockVisibilityChanged(bool visible);
    void onLineWeightToggled(bool checked);
    void onCommandFilterToggled(bool checked);

    // 신규 슬롯
    void onLineTypeDockToggled(bool checked);
    void onLineTypeDockVisibilityChanged(bool visible);
    
    void onLayerTableDockToggled(bool checked);
    void onLayerTableDockVisibilityChanged(bool visible);

    void onBlockPaletteDockToggled(bool checked);
    void onBlockPaletteDockVisibilityChanged(bool visible);

    void onPropertyDockToggled(bool checked);
    void onPropertyDockVisibilityChanged(bool visible);

private:
    CustomMenuBar *m_menuBar;
    CommandDock *m_commandDock;
    QDockWidget *m_lineTypeDock;
    QDockWidget *m_layerTableDock;
    QDockWidget *m_blockPaletteDock;
    QDockWidget *m_propertyDock;
    AppContext *m_appContext;
};

#endif // MENUDOCKSYNCCONTROLLER_H