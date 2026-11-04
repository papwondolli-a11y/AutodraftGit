#ifndef ARRAYTOOL_H
#define ARRAYTOOL_H

#include "abstracttool.h"
#include <QList>

class CadItem;
class MyGraphicsScene;

class ArrayTool : public AbstractTool {
    Q_OBJECT
public:
    explicit ArrayTool(QObject *parent = nullptr);
    ~ArrayTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    bool processCommand(const QString &command) override;

protected:
    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    void keyPress(QKeyEvent *event) override;

private:
    void updatePrompt();
    void clearOverlay();
    void handleSelectionMove(QPointF pos);
    void processBoxSelection(QPointF p2);
    void updateArrayGhost(const QPointF &currentPos);
    void finishArray();

    enum ArrayType { None, Rectangular, Polar };

    enum State {
        State_SelectingObjects,
        State_SelectingObjects_SecondCorner,
        State_SelectingType,
        // Rectangular states
        State_Rect_WaitingNumRows,
        State_Rect_WaitingNumCols,
        State_Rect_WaitingRowSpacing_P1,
        State_Rect_WaitingRowSpacing_P2,
        State_Rect_WaitingColSpacing_P1,
        State_Rect_WaitingColSpacing_P2,
        // Polar states
        State_Polar_WaitingCenter,
        State_Polar_WaitingNumItems,
        State_Polar_WaitingAngle
    };

    State m_currentState = State_SelectingObjects;
    ArrayType m_arrayType = None;

    QList<CadItem *> m_selectedItems;
    QPointF m_firstCorner;

    // Rectangular parameters
    int m_numRows = 1;
    int m_numCols = 1;
    double m_rowSpacing = 0.0;
    double m_colSpacing = 0.0;
    QPointF m_rectBasePoint;
    QPointF m_spacingPoint1;

    // Polar parameters
    QPointF m_centerPoint;
    int m_numItems = 4;
    double m_fillAngle = 360.0;

    QPointF m_currentMousePos;

    // 정적 기본값 (프로그램 실행 중 유지)
    static ArrayType s_lastArrayType;
    static int s_lastNumRows;
    static int s_lastNumCols;
    static double s_lastRowSpacing;
    static double s_lastColSpacing;
    static int s_lastNumItems;
    static double s_lastFillAngle;
};

#endif // ARRAYTOOL_H