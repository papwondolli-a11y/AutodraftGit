#include "doctabwidget.h"
#include "appcontext.h"
#include "drawsubwindow.h"
#include "abstracttool.h"
#include "mygraphicsscene.h"
#include "mygraphicsview.h"
#include "subwindowfloatcontroller.h"
#include <QFrame>
#include <QHBoxLayout>
#include <QMdiArea>
#include <QMdiSubWindow>
#include <QPainter>
#include <QPushButton>
#include <QStyle>
#include <QStyleOptionTab>
#include <QStylePainter>
#include <QTabBar>
#include <QVariant>
#include <QMenu>
#include <QAction>
#include <QTimer>

namespace {
// [신규] QTabBar/문서 스타일시트는 탭별 배경색을 지원하지 않으므로(전체
// QTabBar#DocTabBar::tab 규칙만 존재), 최소화된 서브윈도우에 연결된 탭만
// 직접 페인트하여 파란 배경을 칠하는 QTabBar 서브클래스. tabData에 저장된
// QMdiSubWindow 포인터로 대상 탭을 식별해 인덱스가 바뀌어도(탭 추가/삭제) 안전하다.
class MinimizedDocTabBar : public QTabBar {
public:
  explicit MinimizedDocTabBar(const QSet<QMdiSubWindow *> *minimizedSet,
                               QWidget *parent = nullptr)
      : QTabBar(parent), m_minimizedSet(minimizedSet) {}

protected:
  void paintEvent(QPaintEvent *) override {
    QStylePainter painter(this);
    QStyleOptionTab opt;
    int current = currentIndex();
    for (int i = 0; i < count(); ++i) {
      if (i == current)
        continue;
      initStyleOption(&opt, i);
      paintOneTab(painter, i, opt);
    }
    if (current >= 0 && current < count()) {
      initStyleOption(&opt, current);
      paintOneTab(painter, current, opt);
    }
  }

private:
  void paintOneTab(QStylePainter &painter, int index, QStyleOptionTab &opt) {
    auto *sub = reinterpret_cast<QMdiSubWindow *>(
        tabData(index).value<quintptr>());
    if (sub && m_minimizedSet && m_minimizedSet->contains(sub)) {
      // [핵심] 최소화된 창은 정의상 항상 비활성(화면에 표시되지 않음)이므로
      // 활성/비활성 구분 없이 단일 파란색으로 표시한다.
      painter.save();
      painter.fillRect(opt.rect, QColor(0x2f, 0x6f, 0xed));
      painter.restore();
      painter.drawControl(QStyle::CE_TabBarTabLabel, opt);
    } else {
      painter.drawControl(QStyle::CE_TabBarTab, opt);
    }
  }

  const QSet<QMdiSubWindow *> *m_minimizedSet;
};
} // namespace

DocTabWidget::DocTabWidget(QWidget *parent) : QWidget(parent) {
    setCursor(Qt::ArrowCursor);
    // [핵심] QSS 배경색 적용을 위한 필수 속성 유지
    setAttribute(Qt::WA_StyledBackground, true);
    setObjectName("DocTabWidget"); // 스타일 타겟팅용 이름 부여

    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(5);

    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    // 0. Home 버튼 (고정)
    m_homeButton = new QPushButton(this);
    m_homeButton->setObjectName("DocHomeButton");
    m_homeButton->setText(" Home  ");
    m_homeButton->setFixedSize(40, 25);
    m_homeButton->setCursor(Qt::PointingHandCursor);
    m_homeButton->setFocusPolicy(Qt::NoFocus);

    // 0.5. 구분선
    m_separator = new QFrame(this);
    m_separator->setObjectName("DocSeparator");
    m_separator->setFixedSize(1, 15);

    // 문서 탭바
    // [신규] 최소화된 서브윈도우의 탭 배경을 파란색으로 칠하기 위해 커스텀 서브클래스 사용
    m_docTabBar = new MinimizedDocTabBar(&m_minimizedSubWindows, this);
    m_docTabBar->setObjectName("DocTabBar");
    m_docTabBar->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    m_docTabBar->setMinimumWidth(0);
    m_docTabBar->setUsesScrollButtons(false);
    m_docTabBar->setExpanding(false);
    m_docTabBar->setMovable(false);
    m_docTabBar->setTabsClosable(true);
    m_docTabBar->setDocumentMode(true);
    m_docTabBar->setDrawBase(false);
    m_docTabBar->setContextMenuPolicy(Qt::CustomContextMenu); // [신규] 우클릭 컨텍스트 메뉴 활성화
    m_docTabBar->hide();

    // 새 창(+) 버튼
    m_tabNewButton = new QPushButton(this);
    m_tabNewButton->setObjectName("DocNewButton");
    m_tabNewButton->setText("+");
    m_tabNewButton->setToolTip(tr("New sub window"));
    m_tabNewButton->setCursor(Qt::PointingHandCursor);
    m_tabNewButton->setFocusPolicy(Qt::NoFocus);
    m_tabNewButton->setFixedSize(30, 25);

    layout->addWidget(m_homeButton);
    layout->addWidget(m_separator, 0, Qt::AlignVCenter);
    layout->addSpacing(10);
    layout->addWidget(m_docTabBar);
    layout->addWidget(m_tabNewButton);
    layout->addStretch(1);

    setLayout(layout);

    // [시그널 연결] - 람다 없이 포인터 함수로 잘 연결되어 있음
    connect(m_docTabBar, &QTabBar::tabBarClicked, this, &DocTabWidget::onTabClicked);
    connect(m_docTabBar, &QTabBar::tabCloseRequested, this, &DocTabWidget::onTabCloseRequested);
    connect(m_docTabBar, &QTabBar::customContextMenuRequested, this, &DocTabWidget::onTabBarCustomContextMenuRequested);
    connect(m_tabNewButton, &QPushButton::clicked, this, &DocTabWidget::newWindowRequested);
    connect(m_homeButton, &QPushButton::clicked, this, &DocTabWidget::homeClicked);
}

void DocTabWidget::addDoc(QMdiSubWindow *subWindow) {
    if (!subWindow) return;

    int index = m_docTabBar->addTab(subWindow->windowTitle());
    m_docTabBar->setTabData(index, QVariant::fromValue((quintptr)subWindow));

    if (m_docTabBar->count() > 0)
        m_docTabBar->show();

    connect(subWindow, &QObject::destroyed, this, &DocTabWidget::onSubWindowDestroyed);
    connect(subWindow, &QWidget::windowTitleChanged, this, &DocTabWidget::onWindowTitleChanged);

    m_docTabBar->setCurrentIndex(index);
}

void DocTabWidget::setCurrentDoc(QMdiSubWindow *subWindow) {
    if (!subWindow) return;

    for (int i = 0; i < m_docTabBar->count(); ++i) {
        quintptr ptrVal = m_docTabBar->tabData(i).value<quintptr>();
        if (ptrVal == (quintptr)subWindow) {
            m_docTabBar->setCurrentIndex(i);
            return;
        }
    }
}

void DocTabWidget::updateTabFloatState(QMdiSubWindow *subWindow, bool isDetached) {
    if (!subWindow) return;

    for (int i = 0; i < m_docTabBar->count(); ++i) {
        quintptr ptrVal = m_docTabBar->tabData(i).value<quintptr>();
        if (ptrVal == (quintptr)subWindow) {
            QString title = subWindow->windowTitle();
            if (isDetached) {
                if (!title.startsWith(QStringLiteral("⧉ "))) {
                    m_docTabBar->setTabText(i, QStringLiteral("⧉ ") + title);
                }
            } else {
                if (title.startsWith(QStringLiteral("⧉ "))) {
                    title = title.mid(2);
                }
                m_docTabBar->setTabText(i, title);
            }
            break;
        }
    }
}

void DocTabWidget::updateTabMinimizedState(QMdiSubWindow *subWindow, bool isMinimized) {
    if (!subWindow) return;

    // [신규] 탭 배경을 파란색으로 칠할지 여부를 결정하는 목록 갱신
    if (isMinimized) {
        m_minimizedSubWindows.insert(subWindow);
    } else {
        m_minimizedSubWindows.remove(subWindow);
    }

    for (int i = 0; i < m_docTabBar->count(); ++i) {
        quintptr ptrVal = m_docTabBar->tabData(i).value<quintptr>();
        if (ptrVal == (quintptr)subWindow) {
            // [핵심] "⧉ "(분리) 접두어는 그대로 둔 채 "_ "(최소화) 접두어만 토글
            QString text = m_docTabBar->tabText(i);
            bool hasFloatPrefix = text.startsWith(QStringLiteral("⧉ "));
            QString floatPrefix = hasFloatPrefix ? QStringLiteral("⧉ ") : QString();
            QString rest = hasFloatPrefix ? text.mid(2) : text;
            if (rest.startsWith(QStringLiteral("_ "))) {
                rest = rest.mid(2);
            }
            if (isMinimized) {
                rest = QStringLiteral("_ ") + rest;
            }
            m_docTabBar->setTabText(i, floatPrefix + rest);
            m_docTabBar->setTabToolTip(
                i, isMinimized ? tr("최소화됨 - 클릭하면 복원") : QString());
            break;
        }
    }

    m_docTabBar->update(); // 배경색 갱신을 위한 재도색 요청
}

void DocTabWidget::clickDocTab(QMdiSubWindow *subWindow) {
    if (!subWindow)
        return;

    for (int i = 0; i < m_docTabBar->count(); ++i) {
        quintptr ptrVal = m_docTabBar->tabData(i).value<quintptr>();
        if (ptrVal == (quintptr)subWindow) {
            m_docTabBar->setCurrentIndex(i);
            onTabClicked(i);
            return;
        }
    }
}

void DocTabWidget::onTabClicked(int index) {
    quintptr ptrVal = m_docTabBar->tabData(index).value<quintptr>();
    QMdiSubWindow *subWindow = (QMdiSubWindow *)ptrVal;
    if (subWindow) {
        activateDoc(subWindow);
    }
}

void DocTabWidget::activateDoc(QMdiSubWindow *subWindow) {
    if (!subWindow)
        return;

    // 1. 탭바 선택 인덱스 동기화 (C++11 detach 방지 인덱스 루프)
    for (int i = 0; i < m_docTabBar->count(); ++i) {
        quintptr ptrVal = m_docTabBar->tabData(i).value<quintptr>();
        if (ptrVal == (quintptr)subWindow) {
            m_docTabBar->setCurrentIndex(i);
            break;
        }
    }

    DrawSubWindow *drawSub = qobject_cast<DrawSubWindow *>(subWindow);
    bool isDetached = false;
    if (m_appContext && m_appContext->floatController() && drawSub) {
        isDetached = m_appContext->floatController()->isDetached(drawSub);
    } else {
        isDetached = (subWindow->parentWidget() == nullptr);
    }

    // 분리된 독립 창(Floating)인 경우 최상단으로 띄우고 활성화 (메인 대시보드 화면 유지)
    if (isDetached) {
        if (m_appContext && m_appContext->floatController() && drawSub) {
            m_appContext->floatController()->setActiveFloatingWindow(drawSub);
            m_appContext->setLastActiveWindow(drawSub);
            // [핵심] 활성창 최상단, 비활성 분리창 중간, 대시보드창 최하단 순서로 Z-order 재정렬
            m_appContext->floatController()->raiseFloatingWindows(drawSub);
            // 탭 클릭 마우스 이벤트 종료 후 메인 윈도우가 다시 올라오는 현상 방지 지연 보장
            QTimer::singleShot(30, m_appContext->floatController(), &SubWindowFloatController::onDelayedRaiseFloatingTimeout);
        } else {
            if (subWindow->isMinimized()) {
                subWindow->showNormal();
            }
            subWindow->show();
            subWindow->raise();
            subWindow->activateWindow();
        }
        if (drawSub) {
            if (drawSub->scene() && drawSub->tool()) {
                drawSub->tool()->activate(drawSub->scene());
                drawSub->scene()->setTool(drawSub->tool());
            }
            if (drawSub->view()) {
                drawSub->view()->setFocus();
                drawSub->view()->setCustomCrossCursor();
            }
        }
        return;
    }

    // MDI 내부에 부착된 서브윈도우인 경우에만 작업 영역으로 전환
    emit navWorkspaceRequested();

    // [핵심] 고정창이 위치한 메인 윈도우를 OS 상에서 최상단으로 올림 (분리창 뒤에 숨지 않도록 보장)
    if (QWidget *topWin = window()) {
        topWin->raise();
        topWin->activateWindow();
    }

    // [신규] 부착창 선택 시 분리창 우선 모드 해제
    if (m_appContext && m_appContext->floatController()) {
        m_appContext->floatController()->clearActiveFloatingWindow();
    }

    if (drawSub && m_appContext) {
        m_appContext->setLastActiveWindow(drawSub);
    }

    // [핵심] MDI 영역 활성창 지정
    QMdiArea *mdi = nullptr;
    if (m_appContext && m_appContext->mdiArea()) {
        mdi = m_appContext->mdiArea();
    } else if (subWindow->mdiArea()) {
        mdi = subWindow->mdiArea();
    }

    if (mdi) {
        mdi->setActiveSubWindow(subWindow);
    }

    QRect mdiRect = mdi ? mdi->viewport()->rect() : QRect();
    if (mdiRect.isValid()) {
        subWindow->setGeometry(mdiRect);
    }
    subWindow->show();
    subWindow->showMaximized();
    if (mdiRect.isValid()) {
        subWindow->setGeometry(mdiRect);
    }
    subWindow->raise();
    subWindow->activateWindow();

    // [신규] 탭 클릭으로 최소화된 창을 직접 복원했으므로 최소화 표시 해제
    updateTabMinimizedState(subWindow, false);

    if (subWindow->widget()) {
        subWindow->widget()->show();
    }

    if (drawSub) {
        if (drawSub->scene() && drawSub->tool()) {
            drawSub->tool()->activate(drawSub->scene());
            drawSub->scene()->setTool(drawSub->tool());
        }
        if (drawSub->view()) {
            drawSub->view()->show();
            drawSub->view()->setFocus();
            drawSub->view()->setCustomCrossCursor();
            drawSub->view()->zoomAll();
            drawSub->view()->update();
            if (drawSub->view()->viewport()) {
                drawSub->view()->viewport()->update();
                drawSub->view()->viewport()->repaint();
            }
            drawSub->view()->repaint();
        }
        if (drawSub->scene()) {
            drawSub->scene()->update();
        }
    }

    // [핵심] 뷰포트 지오메트리 계산 지연 대비 2차(80ms) 및 최종 3차(200ms) 보장 타이머 등록
    m_pendingActivateSubWindow = subWindow;
    QTimer::singleShot(80, this, &DocTabWidget::onDelayedActivateDocTimeout);
    QTimer::singleShot(200, this, &DocTabWidget::onFinalActivateDocTimeout);
}

void DocTabWidget::onDelayedActivateDocTimeout() {
    if (!m_pendingActivateSubWindow)
        return;

    QMdiSubWindow *sub = m_pendingActivateSubWindow.data();
    if (!sub)
        return;

    DrawSubWindow *drawSub = qobject_cast<DrawSubWindow *>(sub);
    if (drawSub) {
        if (drawSub->view()) {
            drawSub->view()->zoomAll();
            drawSub->view()->update();
            if (drawSub->view()->viewport()) {
                drawSub->view()->viewport()->repaint();
            }
        }
        if (drawSub->scene()) {
            drawSub->scene()->update();
        }
    }
}

void DocTabWidget::onFinalActivateDocTimeout() {
    if (!m_pendingActivateSubWindow)
        return;

    QMdiSubWindow *sub = m_pendingActivateSubWindow.data();
    if (!sub)
        return;

    DrawSubWindow *drawSub = qobject_cast<DrawSubWindow *>(sub);
    if (drawSub) {
        if (drawSub->scene() && drawSub->tool()) {
            drawSub->tool()->activate(drawSub->scene());
            drawSub->scene()->setTool(drawSub->tool());
        }
        if (drawSub->view()) {
            drawSub->view()->show();
            if (drawSub->view()->viewport() && drawSub->view()->viewport()->underMouse()) {
                drawSub->view()->setFocus();
                drawSub->view()->setCustomCrossCursor();
            }
            drawSub->view()->zoomAll();
            drawSub->view()->update();
            if (drawSub->view()->viewport()) {
                drawSub->view()->viewport()->repaint();
            }
            drawSub->view()->repaint();
        }
        if (drawSub->scene()) {
            drawSub->scene()->update();
        }
    }
}

void DocTabWidget::onTabCloseRequested(int index) {
    quintptr ptrVal = m_docTabBar->tabData(index).value<quintptr>();
    QMdiSubWindow *subWindow = (QMdiSubWindow *)ptrVal;

    if (subWindow) {
        subWindow->close();
    }

    if (m_appContext && m_appContext->mdiArea()) {
        QMdiSubWindow *active = m_appContext->mdiArea()->activeSubWindow();
        if (active) {
            active->show();
            active->showMaximized();
        }
    }
}

void DocTabWidget::onTabBarCustomContextMenuRequested(const QPoint &pos) {
    int tabIndex = m_docTabBar->tabAt(pos);
    if (tabIndex < 0) return;

    quintptr ptrVal = m_docTabBar->tabData(tabIndex).value<quintptr>();
    QMdiSubWindow *subWindow = (QMdiSubWindow *)ptrVal;
    if (!subWindow) return;

    QMenu menu(this);
    DrawSubWindow *drawSub = qobject_cast<DrawSubWindow *>(subWindow);
    bool isDetached = false;
    if (m_appContext && m_appContext->floatController() && drawSub) {
        isDetached = m_appContext->floatController()->isDetached(drawSub);
    } else {
        isDetached = (subWindow->parentWidget() == nullptr);
    }
    QAction *floatAct = menu.addAction(isDetached ? tr("MDI 메인 영역에 도킹") : tr("창 분리 (외부 팝업)"));
    menu.addSeparator();
    QAction *closeAct = menu.addAction(tr("창 닫기"));

    QAction *selected = menu.exec(m_docTabBar->mapToGlobal(pos));
    if (selected == floatAct) {
        emit floatWindowRequested(subWindow);
    } else if (selected == closeAct) {
        subWindow->close();
    }
}

void DocTabWidget::onSubWindowDestroyed(QObject *obj) {
    // [신규] 파괴된 서브윈도우는 최소화 표시 목록에서도 제거 (댕글링 포인터 방지)
    m_minimizedSubWindows.remove(reinterpret_cast<QMdiSubWindow *>(obj));

    for (int i = 0; i < m_docTabBar->count(); ++i) {
        quintptr ptrVal = m_docTabBar->tabData(i).value<quintptr>();
        if (ptrVal == (quintptr)obj) {
            m_docTabBar->removeTab(i);
            break;
        }
    }
    if (m_docTabBar->count() == 0) {
        m_docTabBar->hide();
        // [신규] 모든 탭이 닫히면 컨트롤 버튼도 숨김
        if (m_subControls) {
            m_subControls->hide();
        }
    } else {
        // [버그 수정] destroyed 시그널은 ~QWidget 도중(QMdiArea이 해당 서브윈도우를
        // 정리하는 중)에 호출되므로, 여기서 직접 setActiveSubWindow/showMaximized를
        // 호출하면 파괴 중인 창 상태에서 QMdiArea 내부가 재진입하여 크래시한다.
        // 이벤트 루프로 복귀한 뒤 처리한다.
        QTimer::singleShot(0, this, &DocTabWidget::onShowCurrentDocDeferred);
    }
}

void DocTabWidget::onShowCurrentDocDeferred() {
    if (!m_docTabBar || m_docTabBar->count() == 0)
        return;
    {
        // [핵심] 남은 탭이 있다면 현재 선택된 탭의 서브윈도우를 전면에 표시하여 회색 바탕 방지
        int currentIdx = m_docTabBar->currentIndex();
        if (currentIdx >= 0 && currentIdx < m_docTabBar->count()) {
            quintptr ptrVal = m_docTabBar->tabData(currentIdx).value<quintptr>();
            QMdiSubWindow *sub = (QMdiSubWindow *)ptrVal;
            // 포인터를 역참조하기 전에 아직 MDI 영역에 살아있는 창인지 확인
            bool alive = false;
            if (sub && m_appContext && m_appContext->mdiArea()) {
                alive = m_appContext->mdiArea()->subWindowList().contains(sub);
            }
            if (alive) {
                DrawSubWindow *drawSub = qobject_cast<DrawSubWindow *>(sub);
                bool isDetached = false;
                if (m_appContext && m_appContext->floatController() && drawSub) {
                    isDetached = m_appContext->floatController()->isDetached(drawSub);
                }
                if (!isDetached) {
                    sub->show();
                    sub->showMaximized();
                    sub->raise();
                    if (drawSub && drawSub->view()) {
                        drawSub->view()->show();
                    }
                    if (m_appContext && m_appContext->mdiArea()) {
                        m_appContext->mdiArea()->setActiveSubWindow(sub);
                    }
                }
            }
        }
    }
}

void DocTabWidget::onWindowTitleChanged(const QString &title) {
    QMdiSubWindow *subWindow = qobject_cast<QMdiSubWindow *>(sender());
    if (!subWindow) return;

    for (int i = 0; i < m_docTabBar->count(); ++i) {
        quintptr ptrVal = m_docTabBar->tabData(i).value<quintptr>();
        if (ptrVal == (quintptr)subWindow) {
            m_docTabBar->setTabText(i, title);
            break;
        }
    }
}

void DocTabWidget::setSubControls(QWidget *controls) {
    if (!controls) return;
    m_subControls = controls;
    qobject_cast<QHBoxLayout *>(layout())->addWidget(m_subControls);

    // [신규] 탭 개수가 0이면 컨트롤 버튼을 숨김 처리
    if (m_docTabBar->count() == 0) {
        m_subControls->hide();
    }
}