#ifndef QUADMIRRORDIALOG_H
#define QUADMIRRORDIALOG_H

#include <QDialog>
#include <QPainterPath>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <vector>

class CadItem;
class QToolButton;
class QPushButton;
class QRadioButton;

// [신규] 선택된 객체를 2x2 배열로 미리보기하는 다이얼로그.
// 1(좌상단) 원본 / 2(우상단) 세로축 대칭 / 3(좌하단) 가로축 대칭 /
// 4(우하단) 2번을 다시 가로축으로 대칭한 결과(=원본을 180도 회전한 것과 동일)
// 를 각각 아이콘으로 보여주고, 사용자가 클릭한 방향을 선택하게 한다.
// "4거울상" 명령에서 사용된다.
class QuadMirrorDialog : public QDialog {
  Q_OBJECT
public:
  enum Direction {
    Direction_Original,         // 1: 원본 (참고용, 클릭 불가)
    Direction_MirrorVertical,   // 2: 세로축(수직선) 대칭 -> 좌우 반전
    Direction_MirrorHorizontal, // 3: 가로축(수평선) 대칭 -> 상하 반전
    Direction_Rotate180         // 4: 2번 결과를 가로축으로 한 번 더 대칭
                                 //    (좌우+상하 반전 = 원본 180도 회전과 동일)
  };

  // items : 미러 대상 객체 목록 (원본 유지, 복제본을 만드는 용도)
  // bbox  : 선택 객체 전체를 감싸는 씬 좌표 바운딩 박스 (미러 축 계산 기준)
  explicit QuadMirrorDialog(const std::vector<CadItem *> &items,
                            const QRectF &bbox, QWidget *parent = nullptr);

  // 배치 방식: 이동(원본 삭제 후 배치) / 복사(원본 유지하고 배치)
  enum PlacementMode { Mode_Move, Mode_Copy };

  // 사용자가 확정한 방향 (exec() == QDialog::Accepted 일 때만 의미 있음)
  Direction selectedDirection() const { return m_selectedDirection; }

  // 사용자가 고른 배치 방식 (이동/복사 라디오 버튼 상태)
  PlacementMode selectedMode() const;

  // 세로축(2번 방향) / 가로축(3번 방향) 미러 축의 두 점을 반환.
  // 4번(180도 회전)은 이 두 축을 순서대로 두 번 적용해서 만든다.
  void getVerticalAxis(QPointF &p1, QPointF &p2) const;
  void getHorizontalAxis(QPointF &p1, QPointF &p2) const;

private slots:
  // 각 방향 버튼 클릭 슬롯. 프로젝트 규칙상 람다를 쓰지 않고
  // 방향별로 슬롯 함수를 분리한다. (원본 타일은 클릭 불가라 슬롯 없음)
  void onMirrorVerticalClicked();
  void onMirrorHorizontalClicked();
  void onRotate180Clicked();

private:
  void setupUi(); // 2x2 버튼/레이아웃 구성 및 미리보기 아이콘 생성

  // 방향에 해당하는 변환을 적용한 결과 경로를 계산.
  // Direction_Rotate180 은 세로축 대칭 -> 가로축 대칭을 실제 복제본에
  // 순서대로 적용해서 얻는다 (텍스트 항목은 CadItem::mirror() 구현상
  // 글자 자체는 뒤집히지 않으므로, 두 번 적용해도 문자는 그대로 유지됨).
  QPainterPath buildTransformedPath(Direction dir) const;

  // 특정 방향으로 변환한 결과를 렌더링한 미리보기 이미지 생성
  QPixmap renderPreview(Direction dir, const QSize &size) const;

  std::vector<CadItem *> m_items; // 미러 대상 객체 목록
  QRectF m_bbox;                  // 선택 영역 전체 바운딩 박스 (씬 좌표)
  Direction m_selectedDirection;  // 확정된 방향

  QToolButton *m_btnOriginal;         // 1: 원본 (비활성, 참고용)
  QToolButton *m_btnMirrorVertical;   // 2
  QToolButton *m_btnMirrorHorizontal; // 3
  QToolButton *m_btnRotate180;        // 4

  // [신규] 배치 방식 선택 (취소 버튼 위에 배치)
  QRadioButton *m_radioMove; // 이동: 원본 삭제 후 배치
  QRadioButton *m_radioCopy; // 복사: 원본 유지하고 배치

  QPushButton *m_btnCancel;
};

#endif // QUADMIRRORDIALOG_H
