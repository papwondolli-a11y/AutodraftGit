#include "quadmirrordialog.h"
#include "caditem.h"
#include "myvector.h"

#include <QGridLayout>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPen>
#include <QPushButton>
#include <QRadioButton>
#include <QToolButton>
#include <algorithm>

// 미리보기 아이콘 한 변의 크기 (정사각형 아이콘)
static const int kPreviewIconSize = 120;
// 미리보기 렌더링 시 가장자리에 두는 여백(px)
static const double kPreviewMargin = 10.0;

QuadMirrorDialog::QuadMirrorDialog(const std::vector<CadItem *> &items,
                                   const QRectF &bbox, QWidget *parent)
    : QDialog(parent), m_items(items), m_bbox(bbox),
      m_selectedDirection(Direction_MirrorVertical), m_btnOriginal(nullptr),
      m_btnMirrorVertical(nullptr), m_btnMirrorHorizontal(nullptr),
      m_btnRotate180(nullptr), m_radioMove(nullptr), m_radioCopy(nullptr),
      m_btnCancel(nullptr) {
  setWindowTitle(tr("4거울상 - 방향 선택"));
  setupUi();
}

void QuadMirrorDialog::setupUi() {
  const QSize iconSize(kPreviewIconSize, kPreviewIconSize);

  // 2x2 배열: 1 원본 / 2 세로축 대칭 / 3 가로축 대칭 / 4 180도 회전
  m_btnOriginal = new QToolButton(this);
  m_btnMirrorVertical = new QToolButton(this);
  m_btnMirrorHorizontal = new QToolButton(this);
  m_btnRotate180 = new QToolButton(this);

  m_btnOriginal->setText(tr("1 원본"));
  m_btnMirrorVertical->setText(tr("2 세로축 대칭"));
  m_btnMirrorHorizontal->setText(tr("3 가로축 대칭"));
  m_btnRotate180->setText(tr("4 180도 회전"));

  QToolButton *allButtons[4] = {m_btnOriginal, m_btnMirrorVertical,
                                m_btnMirrorHorizontal, m_btnRotate180};
  for (int i = 0; i < 4; ++i) {
    allButtons[i]->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    allButtons[i]->setIconSize(iconSize);
    allButtons[i]->setFixedSize(iconSize.width() + 20, iconSize.height() + 30);
  }

  // 1번(원본)은 참고용일 뿐 실제로 고를 대상이 아니므로 클릭을 막는다.
  m_btnOriginal->setEnabled(false);

  // 각 타일의 결과를 미리 렌더링하여 버튼 아이콘으로 사용
  m_btnOriginal->setIcon(QIcon(renderPreview(Direction_Original, iconSize)));
  m_btnMirrorVertical->setIcon(
      QIcon(renderPreview(Direction_MirrorVertical, iconSize)));
  m_btnMirrorHorizontal->setIcon(
      QIcon(renderPreview(Direction_MirrorHorizontal, iconSize)));
  m_btnRotate180->setIcon(QIcon(renderPreview(Direction_Rotate180, iconSize)));

  // 업로드된 예시 이미지와 동일한 2x2 배치: (0,0)=1 (0,1)=2 (1,0)=3 (1,1)=4
  QGridLayout *grid = new QGridLayout();
  grid->addWidget(m_btnOriginal, 0, 0, Qt::AlignCenter);
  grid->addWidget(m_btnMirrorVertical, 0, 1, Qt::AlignCenter);
  grid->addWidget(m_btnMirrorHorizontal, 1, 0, Qt::AlignCenter);
  grid->addWidget(m_btnRotate180, 1, 1, Qt::AlignCenter);

  // [신규] 배치 방식 라디오 버튼 (취소 버튼 바로 위에 배치)
  // 같은 부모(this) 아래의 QRadioButton은 Qt가 자동으로 서로 배타적으로
  // 묶어주므로 별도의 QButtonGroup 없이도 하나만 선택된다.
  m_radioMove = new QRadioButton(tr("이동"), this);
  m_radioCopy = new QRadioButton(tr("복사"), this);
  m_radioMove->setChecked(true); // 기본값: 이동

  QHBoxLayout *modeRow = new QHBoxLayout();
  modeRow->addStretch();
  modeRow->addWidget(m_radioMove);
  modeRow->addWidget(m_radioCopy);
  modeRow->addStretch();

  m_btnCancel = new QPushButton(tr("취소"), this);

  QHBoxLayout *bottomRow = new QHBoxLayout();
  bottomRow->addStretch();
  bottomRow->addWidget(m_btnCancel);

  QVBoxLayout *mainLayout = new QVBoxLayout(this);
  mainLayout->addWidget(new QLabel(tr("적용할 모양을 클릭하세요."), this));
  mainLayout->addLayout(grid);
  mainLayout->addLayout(modeRow);
  mainLayout->addLayout(bottomRow);

  // 시그널/슬롯 연결 (람다 금지 규칙에 따라 방향별 멤버 함수 슬롯 사용)
  connect(m_btnMirrorVertical, &QToolButton::clicked, this,
          &QuadMirrorDialog::onMirrorVerticalClicked);
  connect(m_btnMirrorHorizontal, &QToolButton::clicked, this,
          &QuadMirrorDialog::onMirrorHorizontalClicked);
  connect(m_btnRotate180, &QToolButton::clicked, this,
          &QuadMirrorDialog::onRotate180Clicked);
  connect(m_btnCancel, &QPushButton::clicked, this, &QDialog::reject);
}

QuadMirrorDialog::PlacementMode QuadMirrorDialog::selectedMode() const {
  return m_radioCopy->isChecked() ? Mode_Copy : Mode_Move;
}

void QuadMirrorDialog::onMirrorVerticalClicked() {
  m_selectedDirection = Direction_MirrorVertical;
  accept();
}

void QuadMirrorDialog::onMirrorHorizontalClicked() {
  m_selectedDirection = Direction_MirrorHorizontal;
  accept();
}

void QuadMirrorDialog::onRotate180Clicked() {
  m_selectedDirection = Direction_Rotate180;
  accept();
}

void QuadMirrorDialog::getVerticalAxis(QPointF &p1, QPointF &p2) const {
  // 세로축(수직선): 바운딩 박스 중심의 x좌표를 지나는 수직선 -> 좌우 반전
  double cx = m_bbox.center().x();
  p1 = QPointF(cx, m_bbox.top());
  p2 = QPointF(cx, m_bbox.bottom());
}

void QuadMirrorDialog::getHorizontalAxis(QPointF &p1, QPointF &p2) const {
  // 가로축(수평선): 바운딩 박스 중심의 y좌표를 지나는 수평선 -> 상하 반전
  double cy = m_bbox.center().y();
  p1 = QPointF(m_bbox.left(), cy);
  p2 = QPointF(m_bbox.right(), cy);
}

QPainterPath QuadMirrorDialog::buildTransformedPath(Direction dir) const {
  QPointF vP1, vP2, hP1, hP2;
  getVerticalAxis(vP1, vP2);
  getHorizontalAxis(hP1, hP2);
  MyVector mvP1(vP1), mvP2(vP2), mhP1(hP1), mhP2(hP2);

  QPainterPath combined;
  for (size_t i = 0; i < m_items.size(); ++i) {
    CadItem *item = m_items[i];
    if (!item)
      continue;

    switch (dir) {
    case Direction_Original:
      // 이동량 0인 getMovedPath()로 원본 외형을 그대로 얻는다
      combined.addPath(item->getMovedPath(MyVector(0.0, 0.0)));
      break;
    case Direction_MirrorVertical:
      combined.addPath(item->getMirroredPath(mvP1, mvP2));
      break;
    case Direction_MirrorHorizontal:
      combined.addPath(item->getMirroredPath(mhP1, mhP2));
      break;
    case Direction_Rotate180: {
      // 세로축 대칭 -> 가로축 대칭을 실제 복제본에 순서대로 적용.
      // (getMirroredPath는 원본 기준 미리보기만 주므로, 2단계 합성 결과를
      //  얻으려면 임시 복제본에 실제 mirror()를 두 번 호출해야 한다)
      CadItem *temp = item->clone();
      temp->mirror(mvP1, mvP2);
      temp->mirror(mhP1, mhP2);
      combined.addPath(temp->getMovedPath(MyVector(0.0, 0.0)));
      delete temp;
      break;
    }
    }
  }
  return combined;
}

QPixmap QuadMirrorDialog::renderPreview(Direction dir,
                                        const QSize &size) const {
  QPainterPath shapePath = buildTransformedPath(dir);

  QPixmap pixmap(size);
  pixmap.fill(Qt::white);

  QRectF fullRect = shapePath.boundingRect();
  if (fullRect.isEmpty())
    return pixmap; // 그릴 대상이 없으면 빈 배경만 반환

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing, true);

  // 여백을 감안해 fullRect가 아이콘 안에 맞도록 배율 계산
  double availW = static_cast<double>(size.width()) - kPreviewMargin * 2.0;
  double availH = static_cast<double>(size.height()) - kPreviewMargin * 2.0;
  double scale = 1.0;
  if (fullRect.width() > 0.0 && fullRect.height() > 0.0) {
    scale = std::min(availW / fullRect.width(), availH / fullRect.height());
  }

  painter.translate(size.width() / 2.0, size.height() / 2.0);
  // 씬 좌표는 Y-up 이므로 Y축 배율을 음수로 주어 화면(Y-down) 기준으로
  // 올바르게 보이도록 보정한다. (filemanager.cpp의 이미지 저장 시 수직
  // 반전 보정과 동일한 이유)
  painter.scale(scale, -scale);
  painter.translate(-fullRect.center());

  QPen pen(QColor(60, 60, 60));
  pen.setWidthF(1.4 / scale);
  painter.setPen(pen);
  painter.drawPath(shapePath);

  return pixmap;
}
