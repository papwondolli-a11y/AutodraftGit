#ifndef CADPICKER_H
#define CADPICKER_H

#include <QObject>
#include <QPointF>
#include <QPointer>

class QGraphicsItem;
class MyGraphicsScene;
class CadItem;
class HatchItem;
class BaseDimensionItem; // 치수 객체 전방 선언 추가

/**
 * @brief CAD 도면에서 객체를 선택할 때 타입 우선순위와 거리를 고려하여 최적의 객체를 찾아주는 클래스입니다.
 *
 * 사용법:
 * CadPicker picker(scene, this);
 * CadItem* hit = picker.pick(pos, tolerance);
 *
 * 선택 우선순위:
 * 1순위: 일반 CadItem (LineItem, CircleItem 등)
 * 2순위: 치수 객체 (BaseDimensionItem 등)
 * 3순위: HatchItem (다른 후보가 없을 때만 선택)
 */
class CadPicker : public QObject {
    Q_OBJECT
public:
    /**
     * @brief scene 포인터를 주입받아 생성합니다.
     * @param scene 선택 대상이 될 씬 (nullptr 가능, setScene으로 나중에 설정 가능)
     * @param parent Qt 부모 객체
     */
    explicit CadPicker(MyGraphicsScene* scene, QObject* parent = nullptr);

    /**
     * @brief 씬을 교체합니다. (예: 탭 전환 시)
     */
    void setScene(MyGraphicsScene* scene);

    /**
     * @brief 특정 좌표에서 가장 적합한 CadItem을 검색합니다.
     * @param pos   클릭된 좌표 (Scene coordinate)
     * @param tolerance 선택 허용 오차
     * @return 검색된 최적의 CadItem. 없으면 nullptr 반환.
     */
    CadItem* pick(const QPointF& pos, double tolerance) const;

private:
    /**
     * @brief item이 HatchItem의 자식이면 자기 자신을, 그 외에는 최상위 CadItem을 반환합니다.
     */
    CadItem* resolveItem(QGraphicsItem* item) const;

    /**
     * @brief CadItem의 선택 우선순위를 반환합니다. 높을수록 우선 선택됩니다.
     * 일반 CadItem = 2, 치수 객체 = 1, HatchItem = 0
     */
    int pickPriority(const CadItem* item) const;

    QPointer<MyGraphicsScene> m_scene;
};

#endif // CADPICKER_H