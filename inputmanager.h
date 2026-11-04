#ifndef INPUTMANAGER_H
#define INPUTMANAGER_H

#include <QObject>
#include <QPointer>

class AppContext;
class QKeyEvent;
class QEvent;

class InputManager : public QObject {
  Q_OBJECT
public:
  explicit InputManager(AppContext *context, QObject *parent = nullptr);

  bool handleKeyPress(QObject *watched, QKeyEvent *event);
  bool handleEnterKey(QObject *watched, QKeyEvent *event);
  bool eventFilter(QObject *watched, QEvent *event);

private:
  AppContext *m_ctx;
  bool m_isHandling = false;
};

#endif // INPUTMANAGER_H
