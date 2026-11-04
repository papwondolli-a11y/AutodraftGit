#ifndef ANGULARDIMENSIONTOOLA_H
#define ANGULARDIMENSIONTOOLA_H

#include "abstracttool.h"
#include "mydimension.h"

class LineItem;

class AngularDimensionToolA : public AbstractTool {
    Q_OBJECT
public:
    explicit AngularDimensionToolA(QObject *parent = nullptr);
    ~AngularDimensionToolA() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void mouseRelease(QPointF pos, Qt::MouseButton button) override;

    bool processCommand(const QString &command) override;

protected slots:
    void onFinish() override;
    void onCancel() override;

private:
    enum State { WaitLine1, WaitLine2, WaitDimLinePoint };

    void cancelDrawing();
    void finalizeDimension();
    void updateGhostPreview();

    State m_state;
    bool m_isDrawing;
    MyDimension m_data;
    QPointF m_currentMousePos;

    LineItem *m_line1 = nullptr;
    LineItem *m_line2 = nullptr;
};

#endif // ANGULARDIMENSIONTOOLA_H