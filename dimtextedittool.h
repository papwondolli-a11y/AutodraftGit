#ifndef DIMTEXTEDITTOOL_H
#define DIMTEXTEDITTOOL_H

#include "abstracttool.h"

class DimTextEditTool : public AbstractTool {
    Q_OBJECT
public:
    explicit DimTextEditTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;

    bool processCommand(const QString &command) override;

protected:
    void onFinish() override;
    void onCancel() override;
};

#endif // DIMTEXTEDITTOOL_H