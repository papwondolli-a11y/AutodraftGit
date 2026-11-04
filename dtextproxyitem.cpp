#include "dtextproxyitem.h"
#include <QPainter>
#include <QFontMetricsF>

DTextProxyItem::DTextProxyItem(QGraphicsItem *parent)
    : QGraphicsObject(parent), m_cursorPos(0), m_cursorVisible(true) {

    setFlag(QGraphicsItem::ItemIsFocusable, true);
    setFlag(QGraphicsItem::ItemAcceptsInputMethod, true);
    setProperty("noRedirect", true);

    m_blinkTimer = new QTimer(this);
    connect(m_blinkTimer, &QTimer::timeout, this, &DTextProxyItem::blinkCursor);
    m_blinkTimer->start(500);
}

DTextProxyItem::~DTextProxyItem() {
    m_blinkTimer->stop();
}

void DTextProxyItem::inputMethodEvent(QInputMethodEvent *event) {
    prepareGeometryChange();

    if (!event->commitString().isEmpty()) {
        m_text.insert(m_cursorPos, event->commitString());
        m_cursorPos += event->commitString().length();
    }

    m_preeditText = event->preeditString();

    m_cursorVisible = true;
    m_blinkTimer->start(500);

    // 이벤트가 뷰로 누수되는 것을 막기 위해 명시적 accept
    event->accept();
    updateShapeCache();
    update();
}

QVariant DTextProxyItem::inputMethodQuery(Qt::InputMethodQuery query) const {
    if (query == Qt::ImCursorPosition) {
        return m_cursorPos + m_preeditText.length();
    }
    if (query == Qt::ImFont) {
        return m_font;
    }
    return QGraphicsObject::inputMethodQuery(query);
}

void DTextProxyItem::setFont(const QFont &font) {
    prepareGeometryChange();
    m_font = font;
    updateShapeCache();
    update();
}

void DTextProxyItem::setTextColor(const QColor &color) {
    m_color = color;
    updateShapeCache();
    update();
}

void DTextProxyItem::setText(const QString &text) {
    prepareGeometryChange();
    m_text = text;
    m_cursorPos = text.length();
    m_preeditText.clear();
    updateShapeCache();
    update();
}

void DTextProxyItem::insertText(const QString &text) {
    prepareGeometryChange();
    m_text.insert(m_cursorPos, text);
    m_cursorPos += text.length();

    m_cursorVisible = true;
    m_blinkTimer->start(500);
    updateShapeCache();
    update();
}

void DTextProxyItem::backspace() {
    if (m_cursorPos > 0) {
        prepareGeometryChange();
        m_text.remove(m_cursorPos - 1, 1);
        m_cursorPos--;

        m_cursorVisible = true;
        m_blinkTimer->start(500);
        updateShapeCache();
        update();
    }
}

void DTextProxyItem::deleteChar() {
    if (m_cursorPos < m_text.length()) {
        prepareGeometryChange();
        m_text.remove(m_cursorPos, 1);

        m_cursorVisible = true;
        m_blinkTimer->start(500);
        updateShapeCache();
        update();
    }
}

void DTextProxyItem::moveCursorLeft() {
    if (m_cursorPos > 0) {
        m_cursorPos--;
        m_cursorVisible = true;
        m_blinkTimer->start(500);
        updateShapeCache();
        update();
    }
}

void DTextProxyItem::moveCursorRight() {
    if (m_cursorPos < m_text.length()) {
        m_cursorPos++;
        m_cursorVisible = true;
        m_blinkTimer->start(500);
        updateShapeCache();
        update();
    }
}

void DTextProxyItem::moveCursorHome() {
    m_cursorPos = 0;
    m_cursorVisible = true;
    m_blinkTimer->start(500);
    updateShapeCache();
    update();
}

void DTextProxyItem::moveCursorEnd() {
    m_cursorPos = m_text.length();
    m_cursorVisible = true;
    m_blinkTimer->start(500);
    updateShapeCache();
    update();
}

QString DTextProxyItem::displayText() const {
    QString res = m_text;
    res.insert(m_cursorPos, m_preeditText);
    return res;
}

QString DTextProxyItem::text() const {
    return displayText();
}

QRectF DTextProxyItem::boundingRect() const {
    QFontMetricsF fm(m_font);
    QString disp = displayText();

    double width = disp.isEmpty() ? 5.0 : fm.horizontalAdvance(disp);
    double height = fm.height();

    QRectF baseRect(0, -fm.ascent(), width + 10.0, height);

     return baseRect.adjusted(-2, -2, 2, 2);
}

void DTextProxyItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) {
    Q_UNUSED(option);
    Q_UNUSED(widget);

    painter->setFont(m_font);
    painter->setPen(m_color);
    painter->drawText(QPointF(0.0, 0.0), displayText());

    // [핵심] 프린터 출력 시에는 커서(편집 UI)를 그리지 않음
    if (m_cursorVisible && painter->device()->devType() == QInternal::Widget) {
        QFontMetricsF fm(m_font);
        QString leftPart = m_text.left(m_cursorPos) + m_preeditText;
        double cursorX = fm.horizontalAdvance(leftPart);
        double ascent = fm.ascent();
        double descent = fm.descent();

        painter->drawLine(QPointF(cursorX + 1.0, descent), QPointF(cursorX + 1.0, -ascent));

        if (!m_preeditText.isEmpty()) {
            double preeditWidth = fm.horizontalAdvance(m_preeditText);
            painter->drawLine(QPointF(cursorX - preeditWidth + 1.0, 2.0), QPointF(cursorX + 1.0, 2.0));
        }
    }
}

void DTextProxyItem::blinkCursor() {
    m_cursorVisible = !m_cursorVisible;
    updateShapeCache();
    update();
}

void DTextProxyItem::updateShapeCache() {
    QPainterPath path;
    QString disp = displayText();

    if (!disp.isEmpty()) {
        // 텍스트가 바뀔 때 딱 한 번만 무거운 연산(addText)을 수행합니다.
        path.addText(QPointF(0, 0), m_font, disp);
    }
    m_cachedShape = path;
}

QPainterPath DTextProxyItem::shape() const {
    if (displayText().isEmpty()) {
        QPainterPath fallback;
        QFontMetricsF fm(m_font);
        // 글자가 없을 때 커서만 클릭할 수 있도록 기본 사각형 반환
        fallback.addRect(QRectF(0, -fm.ascent(), 5, fm.height()));
        return fallback;
    }

    // [핵심] 단일 사각형(tightBoundingRect)을 반환하는 대신,
    // updateShapeCache()에서 미리 만들어둔 '글자 실제 외곽선' 패스를 반환합니다.
    // 이렇게 하면 'j'와 '가'의 높낮이 차이에 따른 빈 공간이 선택 영역에서 완벽히 제외됩니다.
    return m_cachedShape;
}