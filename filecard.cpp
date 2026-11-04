#include "filecard.h"
#include <QAction>
#include <QContextMenuEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QMenu>
#include <QMouseEvent>
#include <QTimer>
#include <QVBoxLayout>

FileCard::FileCard(const QString &filePath, const QImage &thumb, ViewMode mode,
                   QWidget *parent)
    : QWidget(parent), m_filePath(filePath),
      m_waitingDoubleClickRelease(false),
      m_doubleClickFallbackTimer(nullptr) {
  
  setAttribute(Qt::WA_StyledBackground, true);
  setObjectName("FileCard");

  m_doubleClickFallbackTimer = new QTimer(this);
  m_doubleClickFallbackTimer->setSingleShot(true);
  connect(m_doubleClickFallbackTimer, &QTimer::timeout, this,
          &FileCard::onDoubleClickFallbackTimeout);

  setupUi(thumb, mode);
}

void FileCard::setupUi(const QImage &thumb, ViewMode mode) {
  QString fileName = QFileInfo(m_filePath).fileName();

  if (mode == Preview) {
    QVBoxLayout *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(4);

    // 1. 썸네일
    m_iconLabel = new QLabel(this);
    m_iconLabel->setObjectName("FileCardIcon");
    m_iconLabel->setFixedSize(200, 150);
    m_iconLabel->setAlignment(Qt::AlignCenter);

    if (!thumb.isNull()) {
      m_iconLabel->setPixmap(QPixmap::fromImage(thumb).scaled(
          192, 142, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    } else {
      m_iconLabel->setText(tr("No Preview"));
    }

    // 2. 구분선
    QFrame *topLine = new QFrame(this);
    topLine->setObjectName("FileCardTopLine");
    topLine->setFrameShape(QFrame::HLine);

    // 3. 파일 정보 (이름)
    m_nameLabel = new QLabel(fileName, this);
    m_nameLabel->setObjectName("FileNameLabel");
    m_nameLabel->setWordWrap(true);

    // 4. 파일 정보 (경로)
    m_pathLabel = new QLabel(m_filePath, this);
    m_pathLabel->setObjectName("FilePathLabel");
    m_pathLabel->setWordWrap(true);

    layout->addWidget(m_iconLabel);
    layout->addSpacing(4);
    layout->addWidget(topLine);
    layout->addWidget(m_nameLabel);
    layout->addWidget(m_pathLabel);
    layout->addStretch();

    setFixedSize(220, 250);
  } else {
    // List 모드
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(12, 4, 12, 4);
    layout->setSpacing(10);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setObjectName("FileCardIcon");
    m_iconLabel->setFixedSize(48, 36);
    m_iconLabel->setAlignment(Qt::AlignCenter);

    if (!thumb.isNull()) {
      m_iconLabel->setPixmap(QPixmap::fromImage(thumb).scaled(
          48, 36, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    QVBoxLayout *infoLayout = new QVBoxLayout();
    infoLayout->setSpacing(0);
    infoLayout->setContentsMargins(0, 0, 0, 0);

    m_nameLabel = new QLabel(fileName, this);
    m_nameLabel->setObjectName("FileNameLabel");

    m_pathLabel = new QLabel(m_filePath, this);
    m_pathLabel->setObjectName("FilePathLabel");

    infoLayout->addWidget(m_nameLabel);
    infoLayout->addWidget(m_pathLabel);

    layout->addWidget(m_iconLabel);
    layout->addLayout(infoLayout);
    layout->addStretch();

    setFixedHeight(60);
  }
}

void FileCard::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    emit clicked(m_filePath);
  }
  QWidget::mousePressEvent(event);
}

void FileCard::mouseDoubleClickEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    // [핵심] 더블클릭 발생 시 즉시 도면을 열지 않고, 사용자가 마우스 버튼에서 손가락을 뗄 때까지 대기
    // 손가락을 떼기 전에 화면이 바뀌면 mouseReleaseEvent가 유실되어 Qt 마우스 버튼 상태가 눌림(드래그)으로 고착됨
    m_waitingDoubleClickRelease = true;
    if (m_doubleClickFallbackTimer) {
      m_doubleClickFallbackTimer->start(350); // 안전 타임아웃
    }
    event->accept();
    return;
  }
  QWidget::mouseDoubleClickEvent(event);
}

void FileCard::mouseReleaseEvent(QMouseEvent *event) {
  if (m_waitingDoubleClickRelease && event->button() == Qt::LeftButton) {
    m_waitingDoubleClickRelease = false;
    if (m_doubleClickFallbackTimer) {
      m_doubleClickFallbackTimer->stop();
    }
    event->accept();

    // 마우스 릴리스가 완전히 OS/Qt에 처리된 후 다음 이벤트 루프 턴에 안전하게 시그널 방출
    QTimer::singleShot(10, this, &FileCard::onDoubleClickedTimeout);
    return;
  }
  QWidget::mouseReleaseEvent(event);
}

void FileCard::onDoubleClickFallbackTimeout() {
  if (m_waitingDoubleClickRelease) {
    m_waitingDoubleClickRelease = false;
    onDoubleClickedTimeout();
  }
}

void FileCard::onDoubleClickedTimeout() {
  emit doubleClicked(m_filePath);
}

void FileCard::contextMenuEvent(QContextMenuEvent *event) {
  QMenu menu(this);
  QAction *removeAction = menu.addAction(tr("목록에서 삭제"));
  connect(removeAction, &QAction::triggered, this, &FileCard::onRemoveActionTriggered);
  menu.exec(event->globalPos());
}

void FileCard::onRemoveActionTriggered() {
  emit requestRemove(m_filePath);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void FileCard::enterEvent(QEnterEvent *event) {
#else
void FileCard::enterEvent(QEvent *event) {
#endif
  setCursor(Qt::PointingHandCursor);
  QWidget::enterEvent(event);
}

void FileCard::leaveEvent(QEvent *event) {
  unsetCursor();
  QWidget::leaveEvent(event);
}
