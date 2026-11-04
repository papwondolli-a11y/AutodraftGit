#ifndef SUBWINDOWCONTROLS_H
#define SUBWINDOWCONTROLS_H

#include <QWidget>
// #include <QToolButton>
#include <QHBoxLayout>
#include <QPushButton>

class SubWindowControls : public QWidget {
  Q_OBJECT
public:
  explicit SubWindowControls(QWidget *parent = nullptr);

signals:
  void floatClicked();
  void minimizeClicked();
  void closeClicked();

private:
  QPushButton *m_btnFloat;
  QPushButton *m_btnMin;
  QPushButton *m_btnClose;
};

#endif // SUBWINDOWCONTROLS_H
