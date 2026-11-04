#ifndef TEXTTOOL_H
#define TEXTTOOL_H

#include "abstracttool.h"
#include <QPointer>

class QGraphicsTextItem;

class TextTool : public AbstractTool {
  Q_OBJECT
public:
  explicit TextTool(QObject *parent = nullptr);
  ~TextTool() override;

  void activate(MyGraphicsScene *scene) override;
  void deactivate() override;

  // 마우스 클릭 시 텍스트 입력 박스 생성
  void mousePress(QPointF pos, Qt::MouseButton button) override;

  // Esc 키를 누르면 입력 종료
  void keyPress(QKeyEvent *event) override;

private slots:
  void onFontChanged(const QString &family, qreal size);
  void updatePreviewPos();

private:
  // 텍스트 입력을 확정하고 도면 객체로 고정하는 함수
  void finishEditing();

  // 현재 타이핑 중인 텍스트 아이템을 추적 (QPointer로 안전하게 관리)
  QPointer<QGraphicsTextItem> m_activeTextItem;
  QPointF m_startPos;
};

#endif // TEXTTOOL_H
