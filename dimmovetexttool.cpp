#include "dimmovetexttool.h"
#include "lineardimensionitem.h"
#include "modifyentitycommand.h"
#include "mygraphicsscene.h"
#include "iappcontext.h"
#include "commanddock.h"
#include "mymath.h"      // 위치 계산을 위해 추가
#include <QtMath>        // qDegreesToRadians 등을 위해 추가
#include <QUndoStack>
#include <QPainterPath>
#include <QDebug>
#include <QList>

DimMoveTextTool::DimMoveTextTool(QObject *parent)
    : AbstractTool(parent), m_state(WaitSelect), m_selectedDim(nullptr) {
    // 람다 배제, 포인터 함수 연결
    connect(this, &DimMoveTextTool::toolFinished, this, &DimMoveTextTool::onSelectionFinished);
}

DimMoveTextTool::~DimMoveTextTool() {
    resetTool();
}

void DimMoveTextTool::activate(MyGraphicsScene *scene) {
    if (m_scene == scene) return;
    AbstractTool::activate(scene);

    resetTool();
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->showInteractivePrompt(tr(" 편집할 치수선 객체를 선택하세요 : "), {});
    }
}

void DimMoveTextTool::deactivate() {
    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(false);
    }
    resetTool();
    AbstractTool::deactivate();
}

void DimMoveTextTool::resetTool() {
    m_state = WaitSelect;
    m_selectedDim = nullptr;
    m_oldProps.clear();
    if (m_scene) m_scene->clearOverlay(); // 가상선 지우기
}

void DimMoveTextTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (!m_scene) return;
    if (button != Qt::LeftButton) return;

    IAppContext *ctx = getContext();

    // 1단계: 치수선 객체 선택
    if (m_state == WaitSelect) {
        QGraphicsItem *item = m_scene->itemAt(pos, QTransform());
        if (item) {
            LinearDimensionItem *dimItem = dynamic_cast<LinearDimensionItem*>(item);
            if (dimItem) {
                m_selectedDim = dimItem;
                m_oldProps = dimItem->getProperties();

                // [수정] 가상선 시작점을 기존 치수 문자 위치로 정확히 설정
                if (m_oldProps.value("UseCustomTextPos").toBool()) {
                    // 이미 문자를 이동한 적이 있는 경우, 그 위치에서 시작
                    m_originalTextPos = MyVector(m_oldProps.value("CustomTextPosX").toDouble(),
                                                 m_oldProps.value("CustomTextPosY").toDouble());
                } else {
                    // 처음 이동하는 경우, 치수선의 기본 중앙점(실제 텍스트 위치)을 계산하여 시작
                    MyVector p1(m_oldProps.value("StartX").toDouble(), m_oldProps.value("StartY").toDouble());
                    MyVector p2(m_oldProps.value("EndX").toDouble(), m_oldProps.value("EndY").toDouble());
                    MyVector p3(m_oldProps.value("DimLineX").toDouble(), m_oldProps.value("DimLineY").toDouble());

                    bool useFixedAngle = m_oldProps.value("UseFixedAngle").toBool();
                    double fixedAngle = m_oldProps.value("FixedAngle").toDouble();

                    MyVector v = p2 - p1;
                    double len = v.magnitude();
                    if (len > 1e-6) {
                        MyVector dir;
                        if (useFixedAngle) {
                            double rad = qDegreesToRadians(fixedAngle);
                            dir = MyVector(qCos(rad), qSin(rad));
                            if (MyMath::dotProduct(v, dir) < -1e-9) {
                                dir = dir * -1.0;
                            }
                        } else {
                            dir = v / len;
                        }
                        MyVector normal(-dir.y, dir.x);
                        double h = MyMath::dotProduct(p3 - p1, normal);
                        MyVector d1 = p1 + normal * h;
                        double measurement = useFixedAngle ? std::abs(MyMath::dotProduct(v, dir)) : len;
                        MyVector d2 = d1 + dir * measurement;

                        // 실제 치수선이 렌더링되는 양 끝점의 완벽한 중앙 위치
                        m_originalTextPos = (d1 + d2) * 0.5;
                    } else {
                        m_originalTextPos = p1;
                    }
                }

                // 표시되는 계산된 전체 문자열 가져오기
                m_originalText = m_oldProps["Content"].toString();

                m_state = WaitMovePos;
                if (ctx && ctx->commandDock()) {
                    ctx->commandDock()->showInteractivePrompt(tr(" 치수 이동 방향 및 위치 지정 (마우스 클릭) : "), {});
                }
            }
        }
    }
    // 2단계: 텍스트가 이동할 새로운 위치 지정
    else if (m_state == WaitMovePos) {
        m_newTextPos = MyVector(pos);
        if (m_scene) m_scene->clearOverlay(); // 확정되었으므로 가상선 제거

        m_state = WaitTextInput;
        if (ctx && ctx->commandDock()) {
            QString promptMsg = tr(" 치수문자 입력 <%1> : ").arg(m_originalText);
            ctx->commandDock()->showInteractivePrompt(promptMsg, {});
        }
    }
}

void DimMoveTextTool::mouseMove(QPointF pos) {
    // 텍스트 이동 위치를 지정하는 중일 때 가상선(Rubber band) 렌더링
    if (m_state == WaitMovePos && m_scene) {
        QPainterPath ghostPath;
        ghostPath.moveTo(m_originalTextPos.toPointF());
        ghostPath.lineTo(pos);

        // 노란색 가상선 표시
        m_scene->setGhostPath(ghostPath, true, Qt::yellow, 1.0);
    }
}

bool DimMoveTextTool::processCommand(const QString &command) {
    // 3단계: 문자 입력 처리
    if (m_state == WaitTextInput) {
        QString input = command.trimmed(); // 엔터나 스페이스만 쳤으면 빈 문자열("")이 됨

        QMap<QString, QVariant> newProps = m_oldProps;

        // 커스텀 텍스트 위치 활성화 및 새로운 좌표 적용
        newProps["UseCustomTextPos"] = true;
        newProps["CustomTextPosX"] = m_newTextPos.x;
        newProps["CustomTextPosY"] = m_newTextPos.y;

        // DimMoveTextTool 에 의해 선택/편집된 치수선은 무조건 화살표 반전 적용
        newProps["FlipArrow1"] = true;
        newProps["FlipArrow2"] = true;

        // 사용자가 무언가 타이핑했다면 텍스트 강제 덮어쓰기 (빈칸이면 기존 유지)
        if (!input.isEmpty()) {
            newProps["Text"] = input;
        }

        IAppContext *ctx = getContext();
        if (ctx && ctx->undoStack()) {
            // 속성 변경 커맨드 적용 (Undo/Redo 지원)
            ctx->undoStack()->push(new ModifyEntityCommand(m_selectedDim, m_oldProps, newProps));
        } else if (m_selectedDim) {
            // Undo 스택이 없는 경우의 하드 적용
            // 컨테이너 detach 경고를 방지하기 위해 인덱스 기반 루프 사용
            QList<QString> keys = newProps.keys();
            for (int i = 0; i < keys.size(); ++i) {
                QString key = keys.at(i);
                m_selectedDim->setProperty(key, newProps.value(key));
            }
        }

        if (ctx && ctx->commandDock()) {
            ctx->commandDock()->logMessage(tr("치수 문자가 이동되고 화살표가 반전되었습니다."), CommandDock::Info);
            ctx->commandDock()->setPrompt(" Command : ");
        }

        // 작업 완료 후 도구 초기화 및 종료 시그널 발생
        resetTool();
        emit toolFinished();
        return true; // 커맨드 처리 완료
    }

    return false; // 내가 처리할 커맨드가 아님
}

void DimMoveTextTool::onSelectionFinished() {
 //   qDebug() << "DimMoveTextTool 작업이 안전하게 종료되었습니다.";
}