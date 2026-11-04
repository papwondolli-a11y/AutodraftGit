#ifndef DATUMTOOL_H
#define DATUMTOOL_H

#include "abstracttool.h"
#include "datumitem.h"

class DatumTool : public AbstractTool {
    Q_OBJECT
public:
    explicit DatumTool(QObject *parent = nullptr);

    void activate(MyGraphicsScene *scene) override;
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

private:
    void cancelTool();
    void finalizeDatum();

    enum State {
        WaitSelectGdt,
        WaitStartPoint,
        WaitEndPoint
    };

    State     m_state;
    DatumData m_data;
};

#endif // DATUMTOOL_H