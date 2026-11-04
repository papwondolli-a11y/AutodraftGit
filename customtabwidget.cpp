#include "customtabwidget.h"
#include <QAction>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

CustomTabWidget::CustomTabWidget(QWidget *parent) : QTabWidget(parent) {
    setAttribute(Qt::WA_StyledBackground);
    setMovable(false);
    setTabsClosable(false);

    // [설정 1] 탭 위젯 전체 높이 고정 (80px)
    setFixedHeight(85);

    // 기존에 있던 setStyleSheet(...) 전체 삭제
    // (ThemeManager에서 통합 관리되도록 위임)

    // 탭 추가
    makeTab(tr("그리기"));
    makeTab(tr("편집하기"));
    makeTab(tr(" 보기 "));
    makeTab(tr(" 치수선 "));
}

QWidget *CustomTabWidget::makeTab(const QString &tabName) {
    QMap<QString, QWidget *>::const_iterator it = m_tabMap.constFind(tabName);
    if (it != m_tabMap.constEnd())
        return it.value();

    QWidget *page = new QWidget(this);

    // [설정 2] 레이아웃 여백 최소화
    QVBoxLayout *layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    QToolBar *tb = new QToolBar(page);
    tb->setMovable(false);
    layout->addWidget(tb);

    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setIconSize(QSize(30, 30));

    addTab(page, tabName);

    m_tabMap.insert(tabName, page);
    m_toolBarMap.insert(page, tb);
    return page;
}

QToolBar *CustomTabWidget::makeToolBar(QWidget *tabPage) {
    QMap<QWidget *, QToolBar *>::const_iterator it = m_toolBarMap.constFind(tabPage);
    if (it != m_toolBarMap.constEnd())
        return it.value();

    QToolBar *tb = new QToolBar(tabPage);

    tb->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    tb->setIconSize(QSize(30, 30));

    m_toolBarMap.insert(tabPage, tb);
    return tb;
}

QAction *CustomTabWidget::addCommandAction(const CommandInfo &info) {
    QWidget *page = makeTab(info.mainName());
    QToolBar *tb = makeToolBar(page);

    QAction *act = new QAction(tb);
    act->setText(info.subName());

    if (!info.iconName().isEmpty()) {
        act->setIcon(QIcon(info.iconName()));
    }

    if (!info.shortcut().isEmpty()) {
        act->setShortcut(QKeySequence(info.shortcut()));
        act->setShortcutContext(Qt::ApplicationShortcut);
    }

    this->addAction(act);
    tb->addAction(act);

    return act;
}