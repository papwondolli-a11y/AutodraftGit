#ifndef SCENEPRINTER_H
#define SCENEPRINTER_H

#include <QGraphicsScene>
#include <QWidget>
#include <QRectF>
#include <QColor>
#include <QString>

class QPrinter;
class QPainter;
class LayerManager;

class ScenePrinter {
public:
    // 씬에 그려진 객체들을 프린트 다이얼로그로 프린터를 설정한 뒤 즉시 출력하는 정적 함수.
    // windowRect가 유효하면 그 영역만(윈도우 지정 출력), 아니면 도면 전체를 출력한다.
    static void print(QGraphicsScene* scene, QWidget* parent = nullptr,
                      const QRectF &windowRect = QRectF());

    // [일괄 출력용] 이미 설정이 끝난 printer/painter로 씬 1개(=1페이지)만 렌더링한다.
    // 프린터 다이얼로그를 다시 띄우지 않으므로 여러 도면을 한 번에 이어서 찍을 때 사용한다.
    // 씬이 비어 있으면 아무 것도 그리지 않고 false를 반환한다.
    // windowRect가 유효하면 그 영역만 렌더링한다 (일반적으로는 비워 둬서 전체를 출력).
    static bool renderPage(QGraphicsScene* scene, QPrinter* printer, QPainter* painter,
                           const QRectF &windowRect = QRectF());

    // [공용 규칙] 화면 색상을 인쇄용 색상으로 변환한다: 레이어에 출력 색상이 수동으로
    // 지정돼 있으면 그 색을 그대로, 아니면 밝은 색(휘도 높음)을 자동으로 검정으로 바꾼다.
    // Dimension/GDT처럼 paint() 도중 레이어 색상을 매번 다시 가져오는 항목들도
    // (ScenePrinter가 아이템의 pen()만 임시로 바꿔서는 영향을 줄 수 없으므로) 인쇄 시
    // 이 함수를 직접 호출해서 같은 규칙을 적용해야 한다.
    static QColor resolvePrintColor(const QColor &screenColor, LayerManager *layerManager,
                                    const QString &layerName);

    // [편의 함수] 인쇄 중일 때만 resolvePrintColor()를 적용하고, 화면 표시 중에는 원래 색을
    // 그대로 반환한다. paint() 도중 레이어 색상을 매번 다시 읽어와 그리는 항목들
    // (Dimension, SurfaceFinish 등)이 반복적으로 쓰는 편의 래퍼.
    static QColor resolvePrintColorForPaint(QPainter *painter, const QColor &screenColor,
                                            LayerManager *layerManager, const QString &layerName);

    // [화면 캡처용] 켜 두면 resolvePrintColor()가 인쇄 규칙 없이 화면 색상을 그대로 돌려준다.
    // 씬을 QImage 에 그려 화면과 같은 이미지를 얻을 때(AI 브리지 screenshot) 호출 동안만 켠다.
    // QImage 는 위젯이 아니라서 Dimension 문자 같은 항목이 인쇄로 오인해 밝은 색을 검정으로 바꾸기 때문이다.
    static void setScreenCaptureMode(bool on);
};

#endif // SCENEPRINTER_H
