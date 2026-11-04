#ifndef LEADERTEXTEDITTOOL_H
#define LEADERTEXTEDITTOOL_H

#include "abstracttool.h"
#include <QPointer>

class LeaderItem;
class QGraphicsTextItem;

// TextEditTool과 동일한 인라인 편집 방식을 LeaderItem(지시선/콜아웃)에 적용.
// TextItem과 달리 여러 줄일 수 있어 Enter는 줄바꿈으로 두고, 외부 클릭/Esc로 종료한다.
class LeaderTextEditTool : public AbstractTool {
    Q_OBJECT
public:
    explicit LeaderTextEditTool(LeaderItem *target, QObject *parent = nullptr);
    ~LeaderTextEditTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;

private:
    void finishEditing(bool accepted);
    void updatePos();
    void placeEditor();

    LeaderItem *m_targetItem;
    QGraphicsTextItem *m_editorProxy;
    QString m_originalText;
};

#endif // LEADERTEXTEDITTOOL_H
