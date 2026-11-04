#ifndef LAYERCOMBOBOXMANAGER_H
#define LAYERCOMBOBOXMANAGER_H

#include <QComboBox>
#include <QMap>
#include <QList>
#include <QColor>

#include "linetypemanager.h" // LineTypeManager::LinetypeGroup

class AppContext; // 전방 선언

// 라인타입 콤보박스
class LineTypeComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit LineTypeComboBox(AppContext *context, QWidget *parent = nullptr);
    // group 에 해당하는 선종류만 채운다. 항상 맨 위에 "Continuous"(실선)를 두고 기본 선택.
    void populate(LineTypeManager::LinetypeGroup group =
                      LineTypeManager::LinetypeGroup::AutoDraft);
private:
    AppContext *m_appContext;
};

// 라인두께 콤보박스
class LineWidthComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit LineWidthComboBox(AppContext *context, QWidget *parent = nullptr);
    void populate();
private:
    AppContext *m_appContext;
};

// 컬러 콤보박스
class ColorComboBox : public QComboBox {
    Q_OBJECT
public:
    explicit ColorComboBox(AppContext *context, QWidget *parent = nullptr);
    void populate();
private:
    AppContext *m_appContext;
};

#endif // LAYERCOMBOBOXMANAGER_H
