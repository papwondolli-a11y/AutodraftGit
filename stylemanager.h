#ifndef STYLEMANAGER_H
#define STYLEMANAGER_H

#include <QObject>
#include <QString>
#include <QColor>
#include <QList>

// 구조체 정의
struct SelectColor {
    QString name;
    QColor color;
};

struct LineWeight {
    QString name;
    double weight;
};

class StyleManager : public QObject
{
    Q_OBJECT

public:
    explicit StyleManager(QObject *parent = nullptr);
    ~StyleManager();

    // 데이터를 가져가는 Getter 함수
    QList<SelectColor> getColors() const;
    QList<LineWeight> getLineWeights() const;

    void addCustomColor(const SelectColor& color);

private:
    void initData(); // 초기 데이터 세팅용

private:
    QList<SelectColor> m_colors;
    QList<LineWeight> m_lineWeights;
};

#endif // STYLEMANAGER_H
