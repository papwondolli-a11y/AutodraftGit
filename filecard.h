#ifndef FILECARD_H
#define FILECARD_H

#include <QImage>
#include <QLabel>
#include <QWidget>


class QTimer;

class FileCard : public QWidget {
  Q_OBJECT
public:
  enum ViewMode { List, Preview };
  explicit FileCard(const QString &filePath, const QImage &thumb, ViewMode mode,
                    QWidget *parent = nullptr);

signals:
  void clicked(const QString &filePath);
  void doubleClicked(const QString &filePath);
  void requestRemove(const QString &filePath);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseDoubleClickEvent(QMouseEvent *event) override;
  void contextMenuEvent(QContextMenuEvent *event) override;
  void leaveEvent(QEvent *event) override;
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
  void enterEvent(QEnterEvent *event) override;
#else
  void enterEvent(QEvent *event) override;
#endif

private slots:
  void onRemoveActionTriggered();
  void onDoubleClickedTimeout(); // 더블클릭 비동기 전달 슬롯
  void onDoubleClickFallbackTimeout(); // 더블클릭 릴리스 대기 fallback 슬롯

private:
  void setupUi(const QImage &thumb, ViewMode mode);

  QString m_filePath;
  QLabel *m_iconLabel;
  QLabel *m_nameLabel;
  QLabel *m_pathLabel;
  bool m_waitingDoubleClickRelease;
  QTimer *m_doubleClickFallbackTimer;
};

#endif // FILECARD_H