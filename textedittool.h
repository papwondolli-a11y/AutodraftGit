#ifndef TEXTEDITTOOL_H
#define TEXTEDITTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QPointer>
#include <vector>

class TextItem;
class QGraphicsTextItem;

class TextEditTool : public AbstractTool {
    Q_OBJECT
public:
    explicit TextEditTool(TextItem *target, QObject *parent = nullptr);
    ~TextEditTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;

private:
    void finishEditing(bool accepted);
    void updatePos();
    void placeEditor();

    TextItem *m_targetItem;
    QGraphicsTextItem *m_editorProxy;
    QGraphicsItem *m_originalParent;
    QPointF m_originalPos;
    QString m_originalText;

    // 편집 전 글자 기준 박스 꼭짓점(씬 좌표) — BOXTEXT 박스를 찾아 크기 자동 조정용
    std::vector<MyVector> m_originalBoxCorners;
    double m_boxTolerance;
};

#endif // TEXTEDITTOOL_H