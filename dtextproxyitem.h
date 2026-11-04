#ifndef DTEXTPROXYITEM_H
#define DTEXTPROXYITEM_H

#include <QGraphicsObject>
#include <QFont>
#include <QString>
#include <QTimer>
#include <QInputMethodEvent>

class DTextProxyItem : public QGraphicsObject {
    Q_OBJECT
public:
    explicit DTextProxyItem(QGraphicsItem *parent = nullptr);
    ~DTextProxyItem() override;

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

    void setFont(const QFont &font);
    void setTextColor(const QColor &color);

    void insertText(const QString &text);
    void backspace();
    void deleteChar();

    void moveCursorLeft();
    void moveCursorRight();
    void moveCursorHome();
    void moveCursorEnd();

    QString text() const;
    void setText(const QString &text);

protected:
    void inputMethodEvent(QInputMethodEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;
    QPainterPath shape() const override; // 추가

private slots:
    void blinkCursor();

private:
    QString displayText() const;

    QString m_text;
    QString m_preeditText;
    int m_cursorPos;

    QFont m_font;
    QColor m_color;
    QTimer *m_blinkTimer;
    bool m_cursorVisible;
    void updateShapeCache(); // 패스를 미리 계산해두는 함수
    QPainterPath m_cachedShape; // 계산된 패스를 저장할 변수
};

#endif // DTEXTPROXYITEM_H