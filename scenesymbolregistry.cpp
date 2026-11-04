#include "scenesymbolregistry.h"

// ── SceneSymbolRegistry ───────────────────────────────────────────────────────

void SceneSymbolRegistry::registerSymbol(const QString &uuid, const SymbolInfo &info)
{
    m_data.insert(uuid, info);
}

void SceneSymbolRegistry::unregisterSymbol(const QString &uuid)
{
    m_data.remove(uuid);
}

bool SceneSymbolRegistry::hasSymbol(const QString &uuid) const
{
    return m_data.contains(uuid);
}

SymbolInfo SceneSymbolRegistry::symbolInfo(const QString &uuid) const
{
    return m_data.value(uuid);
}

const QMap<QString, SymbolInfo> &SceneSymbolRegistry::allSymbols() const
{
    return m_data;
}

void SceneSymbolRegistry::clear()
{
    m_data.clear();
}

// ── RegisterSymbolCommand ─────────────────────────────────────────────────────

RegisterSymbolCommand::RegisterSymbolCommand(SceneSymbolRegistry *reg,
                                             const QString       &uuid,
                                             const SymbolInfo    &info,
                                             QUndoCommand        *parent)
    : QUndoCommand(parent)
    , m_registry(reg)
    , m_uuid(uuid)
    , m_info(info)
{}

void RegisterSymbolCommand::redo()
{
    m_registry->registerSymbol(m_uuid, m_info);
}

void RegisterSymbolCommand::undo()
{
    m_registry->unregisterSymbol(m_uuid);
}

// ── UnregisterSymbolCommand ───────────────────────────────────────────────────

UnregisterSymbolCommand::UnregisterSymbolCommand(SceneSymbolRegistry *reg,
                                                 const QString       &uuid,
                                                 const SymbolInfo    &info,
                                                 QUndoCommand        *parent)
    : QUndoCommand(parent)
    , m_registry(reg)
    , m_uuid(uuid)
    , m_info(info)
{}

void UnregisterSymbolCommand::redo()
{
    m_registry->unregisterSymbol(m_uuid);
}

void UnregisterSymbolCommand::undo()
{
    m_registry->registerSymbol(m_uuid, m_info);
}

// ── UpdateRegistryCommand ─────────────────────────────────────────────────────

UpdateRegistryCommand::UpdateRegistryCommand(SceneSymbolRegistry *reg,
                                             const QString       &uuid,
                                             const SymbolInfo    &oldInfo,
                                             const SymbolInfo    &newInfo,
                                             QUndoCommand        *parent)
    : QUndoCommand(parent)
    , m_registry(reg)
    , m_uuid(uuid)
    , m_oldInfo(oldInfo)
    , m_newInfo(newInfo)
{}

void UpdateRegistryCommand::redo()
{
    m_registry->registerSymbol(m_uuid, m_newInfo);
}

void UpdateRegistryCommand::undo()
{
    m_registry->registerSymbol(m_uuid, m_oldInfo);
}
