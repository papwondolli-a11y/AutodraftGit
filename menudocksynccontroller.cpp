#include "menudocksynccontroller.h"
#include "custommenubar.h"
#include "commanddock.h"
#include "appcontext.h"
#include <QDockWidget>
#include <QAction>

MenuDockSyncController::MenuDockSyncController(CustomMenuBar *menuBar, CommandDock *commandDock, 
                                               QDockWidget *lineTypeDock, QDockWidget *layerTableDock,
                                               QDockWidget *blockPaletteDock, QDockWidget *propertyDock,
                                               AppContext *appContext, QObject *parent)
    : QObject(parent), m_menuBar(menuBar), m_commandDock(commandDock), 
      m_lineTypeDock(lineTypeDock), m_layerTableDock(layerTableDock),
      m_blockPaletteDock(blockPaletteDock), m_propertyDock(propertyDock), m_appContext(appContext) {

    if (m_menuBar) {
        // 1. 커맨드 도크 동기화
        if (m_commandDock) {
            connect(m_menuBar, &CustomMenuBar::toggleCommandLine, this, &MenuDockSyncController::onCommandLineToggled);
            // QDockWidget의 toggleViewAction을 사용하여 탭 상태와 무관하게 개폐 상태 추적
            connect(m_commandDock->toggleViewAction(), &QAction::toggled, this, &MenuDockSyncController::onCommandDockVisibilityChanged);
            connect(m_menuBar, &CustomMenuBar::toggleCommandFilter, this, &MenuDockSyncController::onCommandFilterToggled);
            
            m_menuBar->setCommandLineChecked(m_commandDock->toggleViewAction()->isChecked());
            m_menuBar->setCommandFilterChecked(true); // 필터는 기본 활성화
        }

        // 2. 선 두께 동기화
        if (m_appContext) {
            connect(m_menuBar, &CustomMenuBar::toggleLineWeight, this, &MenuDockSyncController::onLineWeightToggled);
        }

        // 3. 선 종류 도크
        if (m_lineTypeDock) {
            connect(m_menuBar, &CustomMenuBar::toggleLineTypeDock, this, &MenuDockSyncController::onLineTypeDockToggled);
            connect(m_lineTypeDock->toggleViewAction(), &QAction::toggled, this, &MenuDockSyncController::onLineTypeDockVisibilityChanged);
            m_menuBar->setLineTypeDockChecked(m_lineTypeDock->toggleViewAction()->isChecked());
        }

        // 4. 레이어 테이블 도크
        if (m_layerTableDock) {
            connect(m_menuBar, &CustomMenuBar::toggleLayerTableDock, this, &MenuDockSyncController::onLayerTableDockToggled);
            connect(m_layerTableDock->toggleViewAction(), &QAction::toggled, this, &MenuDockSyncController::onLayerTableDockVisibilityChanged);
            m_menuBar->setLayerTableDockChecked(m_layerTableDock->toggleViewAction()->isChecked());
        }

        // 5. 블록 팔레트 도크
        if (m_blockPaletteDock) {
            connect(m_menuBar, &CustomMenuBar::toggleBlockPaletteDock, this, &MenuDockSyncController::onBlockPaletteDockToggled);
            connect(m_blockPaletteDock->toggleViewAction(), &QAction::toggled, this, &MenuDockSyncController::onBlockPaletteDockVisibilityChanged);
            m_menuBar->setBlockPaletteDockChecked(m_blockPaletteDock->toggleViewAction()->isChecked());
        }

        // 6. 속성 도크
        if (m_propertyDock) {
            connect(m_menuBar, &CustomMenuBar::togglePropertyDock, this, &MenuDockSyncController::onPropertyDockToggled);
            connect(m_propertyDock->toggleViewAction(), &QAction::toggled, this, &MenuDockSyncController::onPropertyDockVisibilityChanged);
            m_menuBar->setPropertyDockChecked(m_propertyDock->toggleViewAction()->isChecked());
        }
    }
}

void MenuDockSyncController::onCommandLineToggled(bool checked) {
    if (m_commandDock && m_commandDock->isVisible() != checked) {
        m_commandDock->setVisible(checked);
    }
}

void MenuDockSyncController::onCommandDockVisibilityChanged(bool visible) {
    if (m_menuBar) {
        m_menuBar->setCommandLineChecked(visible);
    }
}

void MenuDockSyncController::onLineWeightToggled(bool checked) {
    if (m_appContext) {
        m_appContext->toggleLineWeight(checked);
    }
}

void MenuDockSyncController::onCommandFilterToggled(bool checked) {
    if (m_commandDock) {
        m_commandDock->setFilterEnabled(checked);
    }
}

void MenuDockSyncController::onLineTypeDockToggled(bool checked) {
    if (m_lineTypeDock && m_lineTypeDock->isVisible() != checked) {
        m_lineTypeDock->setVisible(checked);
    }
}

void MenuDockSyncController::onLineTypeDockVisibilityChanged(bool visible) {
    if (m_menuBar) {
        m_menuBar->setLineTypeDockChecked(visible);
    }
}

void MenuDockSyncController::onLayerTableDockToggled(bool checked) {
    if (m_layerTableDock && m_layerTableDock->isVisible() != checked) {
        m_layerTableDock->setVisible(checked);
    }
}

void MenuDockSyncController::onLayerTableDockVisibilityChanged(bool visible) {
    if (m_menuBar) {
        m_menuBar->setLayerTableDockChecked(visible);
    }
}

void MenuDockSyncController::onBlockPaletteDockToggled(bool checked) {
    if (m_blockPaletteDock && m_blockPaletteDock->isVisible() != checked) {
        m_blockPaletteDock->setVisible(checked);
    }
}

void MenuDockSyncController::onBlockPaletteDockVisibilityChanged(bool visible) {
    if (m_menuBar) {
        m_menuBar->setBlockPaletteDockChecked(visible);
    }
}

void MenuDockSyncController::onPropertyDockToggled(bool checked) {
    if (m_propertyDock && m_propertyDock->isVisible() != checked) {
        m_propertyDock->setVisible(checked);
    }
}

void MenuDockSyncController::onPropertyDockVisibilityChanged(bool visible) {
    if (m_menuBar) {
        m_menuBar->setPropertyDockChecked(visible);
    }
}