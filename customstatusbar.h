#ifndef CUSTOMSTATUSBAR_H
#define CUSTOMSTATUSBAR_H

#include "unitconvert.h"
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QStatusBar>


// 전방 선언 (헤더 의존성 줄이기 위해)
class SnapManager;

class CustomStatusBar : public QStatusBar {
  Q_OBJECT
public:
  explicit CustomStatusBar(QWidget *parent = nullptr);

  // 좌표 업데이트 (기존 기능)
  void updateCoordinate(QPointF pos);

  // [신규] 스냅 매니저와 연동하여 버튼들을 생성하고 연결하는 함수
  void setupSnapControls(SnapManager *manager);

  // [신규] 축척 업데이트
  void updateScale(double scale);

  // [신규] 도면 크기 업데이트
  void updateSize(const QString &size);

  // [Phase 4] 활성 도면의 길이 단위(mm / inch) 반영.
  //   좌표 라벨 표기와 단위 인디케이터를 갱신한다. 좌표값 자체는 항상 mm 로
  //   들어오므로 여기서 표시 단위로 환산한다.
  void updateLinearUnit(LinearUnit u);

private:
  QLabel *coordLabel;
  QLabel *scaleLabel;
  QLabel *sizeLabel;
  QLabel *unitLabel;

  LinearUnit m_linearUnit = LinearUnit::Mm;
  QPointF m_lastCoord;

  // 현재 단위의 좌표 접미사 (" mm" / "\"").
  QString coordUnitTag() const;
  // m_lastCoord 를 현재 단위로 다시 렌더링한다.
  void refreshCoordinate();

  // 버튼 생성 헬퍼 함수
  QPushButton *createSnapBtn(const QString &text, QWidget *parent,
                             bool checkable = true);
};

#endif // CUSTOMSTATUSBAR_H
