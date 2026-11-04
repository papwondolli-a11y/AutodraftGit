#ifndef DTEXTTOOL_H
#define DTEXTTOOL_H

#include "abstracttool.h"
#include <QPointer>
#include <QPointF>

class DTextProxyItem;
class QGraphicsItem;

class DTextTool : public AbstractTool {
    Q_OBJECT
public:
    explicit DTextTool(QObject *parent = nullptr);
    ~DTextTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;

private slots:
    void onFontChanged(const QString &family, qreal size);

protected:
    void finishEditing();
    bool isEditing() const { return !m_activeTextItem.isNull(); }

    // [확장 훅] 파생 도구(BOXTEXT 등)가 명령 이름/프롬프트/후처리를 바꿀 수 있도록 함
    virtual QString commandLogName() const { return QStringLiteral("DTEXT Command"); }
    virtual void showStartPrompt();
    // 글자가 놓일 레이어 (기본: 앱의 현재 글자 레이어)
    virtual QString targetTextLayer() const;
    // 글자 객체가 생성되어 Undo 스택에 들어간 직후 호출 (같은 Undo 매크로 안)
    virtual void onTextCommitted(QGraphicsItem *textItem) { Q_UNUSED(textItem); }

private:
    QPointer<DTextProxyItem> m_activeTextItem;
    QPointF m_startPos;
};

#endif // DTEXTTOOL_H