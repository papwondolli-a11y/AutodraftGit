#include "dimtextedittool.h"
#include "appcontext.h"
#include "cadcommands.h"
#include "commanddock.h"
#include "iappcontext.h"
#include "mygraphicsscene.h"
#include "basedimensionitem.h"
#include "dimtexteditdialog.h"
#include <QUndoStack>

DimTextEditTool::DimTextEditTool(QObject *parent)
    : AbstractTool(parent) {}

void DimTextEditTool::activate(MyGraphicsScene *scene) {
    AbstractTool::activate(scene);

    IAppContext *ctx = getContext();
    if (ctx && ctx->commandDock()) {
        ctx->commandDock()->setToolActive(true);
        ctx->commandDock()->logMessage(tr("DIMEDIT (치수 문자 편집)"), CommandDock::Command);
        ctx->commandDock()->showInteractivePrompt(tr(" 편집할 치수문자을 선택 하세요 : "), {});
    }
}

void DimTextEditTool::deactivate() {
    onCancel();
    AbstractTool::deactivate();
}

void DimTextEditTool::mousePress(QPointF pos, Qt::MouseButton button) {
    if (button == Qt::RightButton) {
        onCancel();
        return;
    }
    if (button != Qt::LeftButton || !m_scene) return;

    // 1. 클릭 위치에서 치수선 찾기 (정밀 타겟팅 지원)
    QGraphicsItem *item = m_scene->itemAt(pos, QTransform());
    BaseDimensionItem *dim = dynamic_cast<BaseDimensionItem *>(item);

    if (!dim) {
        QList<QGraphicsItem *> items = m_scene->items(QRectF(pos.x() - 5, pos.y() - 5, 10, 10));
        for (int i = 0; i < items.size(); ++i) {
            if ((dim = dynamic_cast<BaseDimensionItem *>(items[i]))) break;
        }
    }

    // 2. 치수선을 찾았다면 속성을 읽어와 대화상자 표시
    if (dim) {
        const QMap<QString, QVariant> props = dim->getProperties();

        DimTextEditData initialData;
        // [수정] "Content"는 머리말/꼬리말이 이미 합쳐진 문자열이라 그대로 쓰면
        // OK 시 머리말/꼬리말이 중복 표기되는 버그가 있었다. 합쳐지기 전의
        // 순수 본문값(RawValue)을 써야 한다.
        initialData.content      = props.value("RawValue").toString();
        initialData.prefix       = props.value("Prefix").toString();
        initialData.suffix       = props.value("Suffix").toString();
        initialData.tolType      = props.value("TolType").toInt();
        initialData.tolUpper     = props.value("TolUpper").toDouble();
        initialData.tolLower     = props.value("TolLower").toDouble();
        initialData.fitCode      = props.value("FitCode").toString();
        initialData.fitShowValues = props.value("FitShowValues").toBool();
        initialData.tolPrecision = props.value("TolPrecision").toInt();
        initialData.tolUseAngle  = props.value("TolUseAngle").toBool();
        initialData.syncGeometry = true; // 기본값: 치수선 연동 ON

        // 모달 다이얼로그 띄우기
        DimTextEditDialog dlg(initialData);
        if (dlg.exec() == QDialog::Accepted) {
            DimTextEditData resultData = dlg.getData();

            IAppContext *ctx = getContext();
            if (ctx && ctx->undoStack()) {
                ctx->undoStack()->beginMacro(tr("Edit Dimension Text"));

                // 공통 텍스트/공차 속성 변경
                // [수정] "Content"(합쳐진 문자열)가 아니라 "RawValue"(순수 본문값)를 old값으로 써야
                // undo 시에도 머리말/꼬리말 중복 표기 없이 정확히 되돌아간다.
                ctx->undoStack()->push(new ChangePropertyCommand(dim, "Content",      props.value("RawValue"),      resultData.content));
                ctx->undoStack()->push(new ChangePropertyCommand(dim, "Prefix",       props.value("Prefix"),       resultData.prefix));
                ctx->undoStack()->push(new ChangePropertyCommand(dim, "Suffix",       props.value("Suffix"),       resultData.suffix));
                ctx->undoStack()->push(new ChangePropertyCommand(dim, "TolType",      props.value("TolType"),      resultData.tolType));
                ctx->undoStack()->push(new ChangePropertyCommand(dim, "TolUpper",     props.value("TolUpper"),     resultData.tolUpper));
                ctx->undoStack()->push(new ChangePropertyCommand(dim, "TolLower",     props.value("TolLower"),     resultData.tolLower));
                ctx->undoStack()->push(new ChangePropertyCommand(dim, "FitCode",      props.value("FitCode"),      resultData.fitCode));
                ctx->undoStack()->push(new ChangePropertyCommand(dim, "FitShowValues", props.value("FitShowValues"), resultData.fitShowValues));
                ctx->undoStack()->push(new ChangePropertyCommand(dim, "TolPrecision", props.value("TolPrecision"), resultData.tolPrecision));
                ctx->undoStack()->push(new ChangePropertyCommand(dim, "TolUseAngle",  props.value("TolUseAngle"),  resultData.tolUseAngle));

                // [신규] 치수선 연동: 본문 값이 유효한 숫자이면 geometry도 갱신
                if (resultData.syncGeometry && !resultData.content.isEmpty()) {
                    bool ok = false;
                    double newVal = resultData.content.toDouble(&ok);
                    if (ok && newVal > 0.0) {
                        // geometry 변경을 Undo 가능하게 하기 위해 좌표를 직접 기록
                        QVariant oldEndX = props.value("EndX");
                        QVariant oldEndY = props.value("EndY");
                        QVariant oldEnd2X = props.value("End2X");
                        QVariant oldEnd2Y = props.value("End2Y");

                        // 실제 변환 수행 (임시로 적용 후 새 좌표 읽기)
                        dim->applyMeasurementValue(newVal);

                        QMap<QString, QVariant> newProps = dim->getProperties();
                        // geometry를 되돌릴 수 있도록 이전/이후 좌표를 undo 스택에 등록
                        ctx->undoStack()->push(new ChangePropertyCommand(dim, "EndX",  oldEndX,  newProps.value("EndX")));
                        ctx->undoStack()->push(new ChangePropertyCommand(dim, "EndY",  oldEndY,  newProps.value("EndY")));
                        if (props.value("DimType").toString() == "Angular") {
                            ctx->undoStack()->push(new ChangePropertyCommand(dim, "End2X", oldEnd2X, newProps.value("End2X")));
                            ctx->undoStack()->push(new ChangePropertyCommand(dim, "End2Y", oldEnd2Y, newProps.value("End2Y")));
                        }
                        // Content는 geometry 연동 시 자동 측정값 사용 (m_data.text 비움)
                        // → Content 속성은 이미 위에서 resultData.content로 설정했으나,
                        //   geometry 연동 모드에서는 자동 계산이 더 정확하므로 text를 비움
                        dim->setProperty("Content", QString(""));
                    }
                }

                ctx->undoStack()->endMacro();
            } else {
                // UndoStack 없는 경우 직접 적용
                dim->setProperty("Content",      resultData.content);
                dim->setProperty("Prefix",       resultData.prefix);
                dim->setProperty("Suffix",       resultData.suffix);
                dim->setProperty("TolType",      resultData.tolType);
                dim->setProperty("TolUpper",     resultData.tolUpper);
                dim->setProperty("TolLower",     resultData.tolLower);
                dim->setProperty("FitCode",      resultData.fitCode);
                dim->setProperty("FitShowValues", resultData.fitShowValues);
                dim->setProperty("TolPrecision", resultData.tolPrecision);
                dim->setProperty("TolUseAngle",  resultData.tolUseAngle);

                if (resultData.syncGeometry && !resultData.content.isEmpty()) {
                    bool ok = false;
                    double newVal = resultData.content.toDouble(&ok);
                    if (ok && newVal > 0.0) {
                        dim->applyMeasurementValue(newVal);
                        dim->setProperty("Content", QString(""));
                    }
                }
            }
        }

        // 편집 완료 후 다음 객체 선택을 위해 프롬프트 유지
        if (getContext() && getContext()->commandDock()) {
            getContext()->commandDock()->showInteractivePrompt(tr(" 편집할 치수문자을 선택 하세요 : "), {});
        }
    }

}

void DimTextEditTool::mouseMove(QPointF pos) { Q_UNUSED(pos); }
void DimTextEditTool::mouseRelease(QPointF pos, Qt::MouseButton button) { Q_UNUSED(pos); Q_UNUSED(button); }
bool DimTextEditTool::processCommand(const QString &command) { Q_UNUSED(command); return false; }

void DimTextEditTool::onFinish() {
    if (m_scene) m_scene->clearOverlay();
    emit toolFinished();
}

void DimTextEditTool::onCancel() {
    if (m_scene) m_scene->clearOverlay();
    if (getContext() && getContext()->commandDock()) {
        getContext()->commandDock()->setPrompt(" Command : ");
    }
    emit toolFinished();
}