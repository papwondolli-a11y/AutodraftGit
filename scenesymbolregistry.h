#ifndef SCENESYMBOLREGISTRY_H
#define SCENESYMBOLREGISTRY_H

#include <QMap>
#include <QString>
#include <QVariant>
#include <QUndoCommand>

// ─────────────────────────────────────────────────────────────────────────────
// SymbolInfo — 레지스트리에 저장되는 심볼 메타데이터
//   type  : "PlanDoor" | "PlanWindow"
//   name  : 자동 생성 이름 ("문-1", "창문-1")
//   props : 도구별 파라미터 (아래 키 규약 참고)
//
//   PlanDoor  : H(double), V(double), FrameType(int 0~3)
//   PlanWindow: W(double), V(double), D1(double), D2(double)
// ─────────────────────────────────────────────────────────────────────────────
struct SymbolInfo {
    QString                  type;
    QString                  name;
    QMap<QString, QVariant>  props;
};

// ─────────────────────────────────────────────────────────────────────────────
// SceneSymbolRegistry — 씬(MyGraphicsScene)이 소유하는 심볼 메타 저장소
//   아이템에는 UUID(PD_DATA_GROUP_ID)만 태깅하고, 실제 정보는 여기서 관리.
//   아이템이 편집/삭제되어도 레지스트리 정보는 유지됨.
//   파일 닫기(clearAll) 시 clear() 호출로 초기화.
// ─────────────────────────────────────────────────────────────────────────────
class SceneSymbolRegistry {
public:
    void       registerSymbol  (const QString &uuid, const SymbolInfo &info);
    void       unregisterSymbol(const QString &uuid);
    bool       hasSymbol       (const QString &uuid) const;
    SymbolInfo symbolInfo      (const QString &uuid) const;
    const QMap<QString, SymbolInfo> &allSymbols() const;
    void       clear           ();

private:
    QMap<QString, SymbolInfo> m_data;
};

// ─────────────────────────────────────────────────────────────────────────────
// RegisterSymbolCommand — 배치 매크로 안에 함께 push 해서 Undo/Redo 지원
// ─────────────────────────────────────────────────────────────────────────────
class RegisterSymbolCommand : public QUndoCommand {
public:
    RegisterSymbolCommand(SceneSymbolRegistry *reg,
                          const QString       &uuid,
                          const SymbolInfo    &info,
                          QUndoCommand        *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    SceneSymbolRegistry *m_registry;
    QString              m_uuid;
    SymbolInfo           m_info;
};

// ── UnregisterSymbolCommand — 심볼 삭제 시 레지스트리 Undo/Redo 지원
//    redo() = unregisterSymbol  /  undo() = registerSymbol (복원)
class UnregisterSymbolCommand : public QUndoCommand {
public:
    UnregisterSymbolCommand(SceneSymbolRegistry *reg,
                            const QString       &uuid,
                            const SymbolInfo    &info,
                            QUndoCommand        *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    SceneSymbolRegistry *m_registry;
    QString              m_uuid;
    SymbolInfo           m_info;
};

// ── UpdateRegistryCommand — 속성 변경 시 레지스트리 Undo/Redo 지원
class UpdateRegistryCommand : public QUndoCommand {
public:
    UpdateRegistryCommand(SceneSymbolRegistry *reg,
                          const QString       &uuid,
                          const SymbolInfo    &oldInfo,
                          const SymbolInfo    &newInfo,
                          QUndoCommand        *parent = nullptr);
    void redo() override;
    void undo() override;

private:
    SceneSymbolRegistry *m_registry;
    QString              m_uuid;
    SymbolInfo           m_oldInfo;
    SymbolInfo           m_newInfo;
};

#endif // SCENESYMBOLREGISTRY_H
