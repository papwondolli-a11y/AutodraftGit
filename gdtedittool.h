#ifndef GDTEDITTOOL_H
#define GDTEDITTOOL_H

#include "abstracttool.h"
#include <QPointF>

class GdtItem;
class DatumItem;

class GdtEditTool : public AbstractTool {
    Q_OBJECT
public:
    explicit GdtEditTool(QObject *parent = nullptr);
    ~GdtEditTool() override = default;

    void activate(MyGraphicsScene *scene) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void keyPress(QKeyEvent *event) override;

private:
    void cancelTool();
    void editGdtItem(GdtItem *item);
    void editDatumItem(DatumItem *item);
};

#endif // GDTEDITTOOL_H