#include "sceneprinter.h"
#include "mygraphicsscene.h"
#include "caditem.h"
#include "iappcontext.h"
#include "layermanager.h"
#include "layer.h"
#include <QPrinter>
#include <QPrintDialog>
#include <QPainter>
#include <QMessageBox>
#include <QRectF>
#include <QGraphicsItem>
#include <QMap>

// 화면은 어두운 배경을 가정하고 흰색/노랑처럼 밝은 색을 즐겨 쓰지만, 인쇄는 흰 종이 위에 그려진다.
// 특히 흑백(그레이스케일)으로 인쇄하면 밝은 색(휘도가 높은 색)일수록 옅은 회색~흰색에 가깝게
// 변환되어 종이 위에서 안 보이거나 흐리게 보인다. 인쇄 직전에만 임시로 진하게(검정) 바꿔서
// 이 문제를 막는다. 휘도는 사람 눈에 가까운 표준 가중치(ITU-R BT.601)로 계산한다.
static double colorLuminance(const QColor &c) {
    return 0.299 * c.red() + 0.587 * c.green() + 0.114 * c.blue();
}

// 이 값보다 밝으면(휘도 기준) 인쇄용으로 검정으로 치환한다. 순백(255)과 순노랑(약 226)을
// 모두 잡아내면서, 빨강(약 76)·파랑(약 29)처럼 이미 충분히 진한 색은 건드리지 않는 임계값.
static const double kPrintDarkenLuminanceThreshold = 200.0;

// 아이템의 레이어 설정을 참고해 인쇄에 실제로 사용할 색상을 결정한다.
// 1) 레이어에 "출력 색상"이 수동으로 지정돼 있으면 그 색을 그대로 사용
// 2) 아니면 자동 규칙(밝은 색 -> 검정)을 적용
// 3) 둘 다 해당 없으면 화면 색상을 그대로 사용
static bool s_screenCaptureMode = false;

void ScenePrinter::setScreenCaptureMode(bool on) {
    s_screenCaptureMode = on;
}

QColor ScenePrinter::resolvePrintColor(const QColor &screenColor, LayerManager *layerManager,
                                       const QString &layerName) {
    if (s_screenCaptureMode) {
        return screenColor;
    }
    if (layerManager) {
        if (Layer *layer = layerManager->getLayer(layerName)) {
            if (layer->hasPrintColorOverride()) {
                return layer->printColor();
            }
        }
    }

    if (colorLuminance(screenColor) > kPrintDarkenLuminanceThreshold) {
        return Qt::black;
    }
    return screenColor;
}

QColor ScenePrinter::resolvePrintColorForPaint(QPainter *painter, const QColor &screenColor,
                                               LayerManager *layerManager, const QString &layerName) {
    if (!painter || painter->device()->devType() == QInternal::Widget) {
        return screenColor;
    }
    return resolvePrintColor(screenColor, layerManager, layerName);
}

bool ScenePrinter::renderPage(QGraphicsScene* scene, QPrinter* printer, QPainter* painter,
                              const QRectF &windowRect) {
    if (!scene || !printer || !painter) {
        return false;
    }

    // 객체들이 포함된 실제 영역 (빈 씬 판정 및, windowRect 미지정 시 출력 영역으로 사용)
    QRectF itemsRect = scene->itemsBoundingRect();
    if (itemsRect.isNull() || itemsRect.isEmpty()) {
        return false;
    }

    // [윈도우 지정 출력] 유효한 영역이 지정됐으면 그 영역만, 아니면 도면 전체를 출력한다.
    QRectF sceneRect = (windowRect.isValid() && !windowRect.isEmpty()) ? windowRect : itemsRect;

    // 안티앨리어싱 설정으로 부드러운 선/텍스트 렌더링 유지
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::TextAntialiasing);

    // 프린터 종이의 출력 가능 영역(여백 제외)
    QRectF pageRect = printer->pageRect(QPrinter::DevicePixel);

    // [수정] 출력 시 배경색 무시 (투명하게 처리하여 종이의 원래 바탕을 사용함)
    QBrush oldBackground = scene->backgroundBrush();
    scene->setBackgroundBrush(Qt::NoBrush);

    // [핵심] 프린트 시 강제로 선가중치(선두께 + 라인타입 해상도) 켜기
    bool oldShowWeight = false;
    MyGraphicsScene* myScene = dynamic_cast<MyGraphicsScene*>(scene);
    if (myScene) {
        oldShowWeight = myScene->showLineWeight();
        myScene->setShowLineWeight(true);
    }

    LayerManager *layerManager = nullptr;
    if (myScene && myScene->context()) {
        layerManager = myScene->context()->layerManager();
    }

    // [핵심] CAD 기본 뷰포트(Y좌표 위로 증가)와 똑같이 출력하기 위해 Y축을 거울처럼 상하단 반전시킵니다.
    // 이를 누락하면 도면이 위아래가 뒤집힌 채 출력되어 종이를 돌려보면 좌우가 뒤바뀐(거울상) 것처럼 보입니다.
    painter->save();
    painter->translate(pageRect.center());
    painter->scale(1.0, -1.0);
    painter->translate(-pageRect.center());

    // [핵심] 인쇄 시 선택 상태를 임시 해제하여 하이라이트 완전 차단
    QList<QGraphicsItem *> selectedItems = scene->selectedItems();
    for (int i = 0; i < selectedItems.size(); ++i) {
        selectedItems[i]->setSelected(false);
    }

    // [핵심] 두 가지를 한 번의 순회로 처리한다:
    //  1) 레이어의 "출력(Plot)" 옵션이 꺼져 있으면 해당 아이템은 인쇄에서 제외 (임시로 숨김)
    //  2) 인쇄 색상 결정 (레이어별 수동 지정 색 우선, 없으면 밝은 색 자동 보정)
    // 각 CadItem은 paint()에서 이 m_pen(색상)을 그대로 참조하므로(선, 채우기, 텍스트 등 모두),
    // 항목별 paint() 코드를 건드리지 않고 여기서 일괄 적용된다.
    QMap<CadItem *, QPen> originalPens;
    QList<CadItem *> hiddenForPlotOff;
    QList<QGraphicsItem *> allItems = scene->items();
    for (int i = 0; i < allItems.size(); ++i) {
        CadItem *ci = dynamic_cast<CadItem *>(allItems.at(i));
        if (!ci) continue;

        if (layerManager) {
            if (Layer *layer = layerManager->getLayer(ci->layerName())) {
                if (!layer->isPlotOn() && ci->isVisible()) {
                    hiddenForPlotOff.append(ci);
                    ci->setVisible(false);
                    continue; // 어차피 안 보이므로 색상 보정은 불필요
                }
            }
        }

        QPen p = ci->pen();
        QColor printColor = resolvePrintColor(p.color(), layerManager, ci->layerName());
        if (printColor != p.color()) {
            originalPens.insert(ci, p);
            QPen adjusted = p;
            adjusted.setColor(printColor);
            ci->setPen(adjusted);
        }
    }

    // 씬에 그려진 모든 아이템을 프린터의 크기에 맞게(KeepAspectRatio) 렌더링
    scene->render(painter, pageRect, sceneRect, Qt::KeepAspectRatio);

    painter->restore(); // Y축 반전 복구

    // 선택 상태 복구
    for (int i = 0; i < selectedItems.size(); ++i) {
        selectedItems[i]->setSelected(true);
    }

    // 인쇄용으로 임시로 바꿨던 펜 색상 복구
    for (auto it = originalPens.constBegin(); it != originalPens.constEnd(); ++it) {
        it.key()->setPen(it.value());
    }

    // 출력(Plot) 꺼짐으로 숨겼던 아이템 복구
    for (int i = 0; i < hiddenForPlotOff.size(); ++i) {
        hiddenForPlotOff[i]->setVisible(true);
    }

    // 상태 원상 복구
    scene->setBackgroundBrush(oldBackground);
    if (myScene) {
        myScene->setShowLineWeight(oldShowWeight);
    }

    return true;
}

void ScenePrinter::print(QGraphicsScene* scene, QWidget* parent, const QRectF &windowRect) {
    if (!scene) {
        QMessageBox::warning(parent, QObject::tr("프린트 오류"), QObject::tr("출력할 씬(Scene)이 없습니다."));
        return;
    }

    // 객체들이 포함된 실제 영역만 계산 (빈 씬인 경우 리턴)
    QRectF sceneRect = scene->itemsBoundingRect();
    if (sceneRect.isNull() || sceneRect.isEmpty()) {
        QMessageBox::information(parent, QObject::tr("알림"), QObject::tr("출력할 객체가 없습니다."));
        return;
    }

    // 고해상도 프린터 설정으로 생성
    QPrinter printer(QPrinter::HighResolution);

    // 기본적으로 가로 방향(Landscape)으로 출력 방향 설정
    printer.setPageOrientation(QPageLayout::Landscape);

    // 프린트 설정 다이얼로그 표시
    QPrintDialog dialog(&printer, parent);
    if (dialog.exec() == QDialog::Accepted) {
        QPainter painter;
        if (!painter.begin(&printer)) {
            QMessageBox::warning(parent, QObject::tr("프린트 오류"), QObject::tr("프린터를 시작할 수 없습니다."));
            return;
        }

        renderPage(scene, &printer, &painter, windowRect);

        painter.end();
    }
}
