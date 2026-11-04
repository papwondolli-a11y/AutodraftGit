#include "clickablecombobox.h"

ClickableComboBox::ClickableComboBox(QWidget *parent)
    : QComboBox(parent) {
}

ClickableComboBox::~ClickableComboBox() {
}

void ClickableComboBox::showEvent(QShowEvent *event) {
    QComboBox::showEvent(event);
    installLineEditFilter();
}

void ClickableComboBox::installLineEditFilter() {
    QLineEdit *le = this->lineEdit();
    if (le) {
        le->removeEventFilter(this);
        le->installEventFilter(this);
    }
}

bool ClickableComboBox::eventFilter(QObject *watched, QEvent *event) {
    QLineEdit *le = this->lineEdit();
    if (watched == le) {
        // 읽기 전용 상태일 때만 동작하도록 함 (사용자 요청 사항)
        if (le && le->isReadOnly()) {
            if (event->type() == QEvent::MouseButtonPress) {
                // 이벤트를 가로채고 직접 팝업 호출
                this->showPopup();
                return true; // 이벤트 버블링 차단
            }
        }
    }
    return QComboBox::eventFilter(watched, event);
}
