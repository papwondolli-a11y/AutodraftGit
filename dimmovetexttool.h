#ifndef DIMMOVETEXTTOOL_H
#define DIMMOVETEXTTOOL_H

#include "abstracttool.h"
#include "myvector.h"
#include <QMap>
#include <QVariant>

class MyGraphicsScene;
class LinearDimensionItem;

class DimMoveTextTool : public AbstractTool {
    Q_OBJECT

public:
    explicit DimMoveTextTool(QObject *parent = nullptr);
    ~DimMoveTextTool() override;

    void activate(MyGraphicsScene *scene) override;
    void deactivate() override;

    void mousePress(QPointF pos, Qt::MouseButton button) override;
    void mouseMove(QPointF pos) override;
    bool processCommand(const QString &command) override;

private slots:
    // 포인터 함수 연결용 슬롯
    void onSelectionFinished();

private:
    // 상태 머신 정의
    enum State {
        WaitSelect,     // 치수선 객체 선택 대기
        WaitMovePos,    // 문자 이동 방향(마우스 클릭) 대기
        WaitTextInput   // 새로운 치수 문자 입력 대기
    };

    State m_state;
    LinearDimensionItem *m_selectedDim;
    QMap<QString, QVariant> m_oldProps;

    MyVector m_originalTextPos; // 가상선(Rubber band) 시작점
    MyVector m_newTextPos;      // 마우스로 클릭한 새로운 위치
    QString m_originalText;     // 프롬프트에 표시할 기존 치수 문자

    void resetTool();
};

#endif // DIMMOVETEXTTOOL_H