#include "filemanager.h"
#include "appcontext.h"
#include "blockitem.h"
#include "caditem.h"
#include "entityfactory.h"
#include "gdtitem.h"
#include "datumitem.h" // [신규 추가] 데이텀 헤더 포함
#include "surfacefinishitem.h"
#include "holedata.h"
#include "holeitem.h"
#include "gripitem.h"
#include "iappcontext.h"
#include "layer.h"
#include "layermanager.h"
#include "linetypemanager.h"
#include "mygraphicsscene.h"
#include "polylineitem.h"
#include "textitem.h"
#include "splineitem.h"
#include "leaderitem.h"
#include "myspline.h"
#include "ellipseitem.h"
#include "myellipse.h"
#include "multilinedata.h"
#include "detailregiondata.h"
#include "twolinetool.h"
#include "planarchdata.h"
#include "scenesymbolregistry.h"
#include <QBuffer>
#include <QDataStream>
#include <QFile>
#include <QGraphicsItem>
#include <QGraphicsTextItem>
#include <QGraphicsView>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QPainter>
#include <QProgressDialog>
#include <QSet>
#include <QSettings>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUuid>

FileManager::FileManager(IAppContext *ctx, QObject *parent)
    : QObject(parent), m_ctx(ctx) {}

FileManager::~FileManager() {}

bool FileManager::initDatabase(const QString &dbPath) {
//    qDebug() << "FileManager: initDatabase START -" << dbPath;
    // [개선] 기존 동일 이름의 연결이 있으며 안전하게 제거
    if (QSqlDatabase::contains("save_connection")) {
        {
            QSqlDatabase oldDb = QSqlDatabase::database("save_connection");
            if (oldDb.isOpen())
                oldDb.close();
        }
        QSqlDatabase::removeDatabase("save_connection");
 //       qDebug() << "FileManager: Old save_connection removed.";
    }

    QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "save_connection");
    db.setDatabaseName(dbPath);

    if (!db.open()) {
//        qDebug() << "Failed to open database:" << db.lastError().text();
        return false;
    }

    QSqlQuery query(db);
    // 레이어 테이블 추가
    query.exec("CREATE TABLE IF NOT EXISTS layers ("
               "name TEXT PRIMARY KEY,"
               "color TEXT,"
               "weight REAL,"
               "linetype TEXT,"
               "is_on INTEGER,"
               "is_plot INTEGER,"
               "is_lock INTEGER,"
               "print_color TEXT"
               ")");

    // [출력 스타일] 기존 파일(이 컬럼이 생기기 전에 저장된 .cdb)과의 호환을 위한 마이그레이션.
    // 컬럼이 이미 있으면 실패하는데, 그 실패는 의도된 것이므로 결과를 확인하지 않는다.
    query.exec("ALTER TABLE layers ADD COLUMN print_color TEXT");

    // 엔티티 테이블: 메타데이터 저장
    query.exec("CREATE TABLE IF NOT EXISTS entities ("
               "id INTEGER PRIMARY KEY AUTOINCREMENT,"
               "type TEXT,"
               "layer TEXT,"
               "properties TEXT," // JSON 메타데이터
               "FOREIGN KEY(layer) REFERENCES layers(name)"
               ")");

    // 기하 데이터 테이블: BLOB으로 바이너리 데이터 저장
    query.exec("CREATE TABLE IF NOT EXISTS geometry ("
               "entity_id INTEGER,"
               "data BLOB,"
               "FOREIGN KEY(entity_id) REFERENCES entities(id)"
               ")");

    // 파일 메타데이터 (미리보기 등)
    query.exec("CREATE TABLE IF NOT EXISTS file_metadata ("
               "key TEXT PRIMARY KEY,"
               "value BLOB"
               ")");

    return true;
}

bool FileManager::saveDrawing(const QString &filePath, MyGraphicsScene *scene) {
    if (!scene || !m_ctx)
        return false;

    // [개선 1: Atomic Save] 원본 파일 손상 방지를 위해 임시 파일(.tmp)에 먼저 저장합니다.
    QString tempFilePath = filePath + ".tmp";

    // 만약 이전 작업 중 생성된 임시 파일이 남아있다면 삭제
    if (QFile::exists(tempFilePath)) {
        QFile::remove(tempFilePath);
    }

    // 원본이 아닌 '임시 파일' 경로로 DB를 초기화합니다.
    if (!initDatabase(tempFilePath))
        return false;

    {
        QSqlDatabase db = QSqlDatabase::database("save_connection");
        if (!db.isOpen())
            return false;

        if (!db.transaction())
            return false;

        QSqlQuery query(db);
        query.exec("DELETE FROM layers");
        query.exec("DELETE FROM entities");
        query.exec("DELETE FROM geometry");
        query.exec("DELETE FROM file_metadata");

        // [개선 2: 포맷 버저닝] 파일 포맷 버전을 메타데이터에 기록하여 훗날 하위 호환성을 대비합니다.
        query.prepare("INSERT INTO file_metadata (key, value) VALUES ('file_version', '1.0')");
        if (!query.exec()) { db.rollback(); return false; }

        // 1. 레이어 저장 (C++11 Range-loop -> Index 기반 루프 변경)
        if (m_ctx->layerManager()) {
            QList<Layer> layers = m_ctx->layerManager()->getAllLayers();
            for (int i = 0; i < layers.size(); ++i) {
                const Layer &layer = layers[i];
                query.prepare(
                    "INSERT INTO layers (name, color, weight, linetype, is_on, "
                    "is_plot, is_lock, print_color) "
                    "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
                query.addBindValue(layer.name());
                query.addBindValue(layer.color().name());
                query.addBindValue(layer.weight());
                query.addBindValue(layer.linetype());
                query.addBindValue(layer.isLayerOn() ? 1 : 0);
                query.addBindValue(layer.isPlotOn() ? 1 : 0);
                query.addBindValue(layer.isLayerLock() ? 1 : 0);
                query.addBindValue(layer.hasPrintColorOverride() ? layer.printColor().name() : QString());

                // [개선 3: 명시적 롤백] 쿼리 실패 시 즉시 롤백하여 DB 락/손상 방지
                if (!query.exec()) { db.rollback(); return false; }
            }
        }

        // 2. 엔티티 저장 (C++11 Range-loop -> Index 기반 루프 변경)
        QList<QGraphicsItem*> items = scene->items();
        for (int i = 0; i < items.size(); ++i) {
            QGraphicsItem *item = items[i];

            // 블록 내부에 포함된 자식 객체는 개별 저장에서 제외
            if (dynamic_cast<BlockItem *>(item->parentItem()) != nullptr)
                continue;

            CadItem *cadItem = dynamic_cast<CadItem *>(item);
            if (!cadItem)
                continue;

            QJsonObject propObj;
            QByteArray geoData;
            QDataStream out(&geoData, QIODevice::WriteOnly);

            // [개선 4: QDataStream 버전 고정] 영구적인 도면 호환성을 위해 6.0으로 고정
            out.setVersion(QDataStream::Qt_6_0);

            serializeCadItem(cadItem, out, propObj, QPointF(0, 0));

            QString type = propObj["Type"].toString();
            QString layer = cadItem->layerName();
            QJsonDocument propDoc(propObj);

            query.prepare(
                "INSERT INTO entities (type, layer, properties) VALUES (?, ?, ?)");
            query.addBindValue(type);
            query.addBindValue(layer);
            query.addBindValue(QString(propDoc.toJson(QJsonDocument::Compact)));

            if (query.exec()) {
                qint64 lastId = query.lastInsertId().toLongLong();
                QSqlQuery geoQuery(db);
                geoQuery.prepare(
                    "INSERT INTO geometry (entity_id, data) VALUES (?, ?)");
                geoQuery.addBindValue(lastId);
                geoQuery.addBindValue(geoData);
                if (!geoQuery.exec()) { db.rollback(); return false; }
            } else {
                db.rollback();
                return false;
            }
        }

        // 3. 썸네일(Preview) 캡쳐 및 저장
        QByteArray preview = capturePreview(scene);
        if (!preview.isEmpty()) {
            query.prepare(
                "INSERT INTO file_metadata (key, value) VALUES ('preview', ?)");
            query.addBindValue(preview);
            if (!query.exec()) { db.rollback(); return false; }
        }

        // 4. 기타 도면 설정 및 메타데이터 저장
        if (m_ctx->layerManager()) {
            Layer *current = m_ctx->layerManager()->getCurrentLayer();
            if (current) {
                query.prepare("INSERT INTO file_metadata (key, value) VALUES "
                              "('current_layer', ?)");
                query.addBindValue(current->name().toUtf8());
                query.exec();
            }
        }

        if (m_ctx) {
            QString textLayer = m_ctx->textLayerName();
            if (!textLayer.isEmpty()) {
                query.prepare(
                    "INSERT INTO file_metadata (key, value) VALUES ('text_layer', ?)");
                query.addBindValue(textLayer.toUtf8());
                query.exec();
            }
        }

        // 치수 스타일 및 도면 Limits 저장
        QJsonObject styleObj = scene->dimensionStyle().toJson();
        QJsonDocument styleDoc(styleObj);
        query.prepare("INSERT INTO file_metadata (key, value) VALUES "
                      "('dimension_style', ?)");
        query.addBindValue(styleDoc.toJson(QJsonDocument::Compact));
        query.exec();

        // 테마 정보 저장
        if (m_ctx->themeManager()) {
            int themeVal = static_cast<int>(m_ctx->themeManager()->currentTheme());
            query.prepare("INSERT INTO file_metadata (key, value) VALUES "
                          "('theme', ?)");
            query.addBindValue(QString::number(themeVal).toUtf8());
            query.exec();
        }

        // [3-3] 다중선 Setup 값은 더 이상 별도 'multiline_setup' 키로 저장하지 않는다.
        //   TwoLineTool 이 Setup 확정 시 scene->documentSettings()("TwoLineTool/*") 에
        //   기록하므로, 아래 doc_settings 직렬화에 자연히 포함된다.

        // [3-1] 도면 스코프 도구 기본값 (DocumentSettings) 직렬화.
        //   템플릿(.cdt)도 .cdb 이므로 이 블롭을 그대로 운반한다.
        {
            const QJsonObject dsObj = scene->documentSettings()->toJson();
            if (!dsObj.isEmpty()) {
                QJsonDocument dsDoc(dsObj);
                query.prepare("INSERT INTO file_metadata (key, value) VALUES "
                              "('doc_settings', ?)");
                query.addBindValue(dsDoc.toJson(QJsonDocument::Compact));
                query.exec();
            }
        }

        // 심볼 레지스트리 저장 (PlanDoor / PlanWindow 속성)
        {
            const auto &regMap = scene->symbolRegistry()->allSymbols();
            if (!regMap.isEmpty()) {
                QJsonObject regObj;
                for (auto it = regMap.constBegin(); it != regMap.constEnd(); ++it) {
                    const SymbolInfo &info = it.value();
                    QJsonObject infoObj;
                    infoObj["type"] = info.type;
                    infoObj["name"] = info.name;
                    QJsonObject propsObj;
                    for (auto pit = info.props.constBegin(); pit != info.props.constEnd(); ++pit)
                        propsObj.insert(pit.key(), QJsonValue::fromVariant(pit.value()));
                    infoObj["props"] = propsObj;
                    regObj.insert(it.key(), infoObj);
                }
                QJsonDocument regDoc(regObj);
                query.prepare("INSERT INTO file_metadata (key, value) VALUES ('symbol_registry', ?)");
                query.addBindValue(regDoc.toJson(QJsonDocument::Compact));
                query.exec();
            }
        }

        query.prepare("INSERT INTO file_metadata (key, value) VALUES ('drawing_scale', ?)");
        query.addBindValue(QString::number(scene->drawingScale()));
        query.exec();

        query.prepare("INSERT INTO file_metadata (key, value) VALUES ('drawing_size_name', ?)");
        query.addBindValue(scene->drawingSizeName().toUtf8());
        query.exec();

        QRectF limits = scene->drawingLimits();
        QJsonObject limitsObj;
        limitsObj.insert("x", limits.x());
        limitsObj.insert("y", limits.y());
        limitsObj.insert("w", limits.width());
        limitsObj.insert("h", limits.height());
        QJsonDocument limitsDoc(limitsObj);
        query.prepare("INSERT INTO file_metadata (key, value) VALUES ('drawing_limits', ?)");
        query.addBindValue(limitsDoc.toJson(QJsonDocument::Compact));
        query.exec();

        db.commit();
        db.close();
    }

    QSqlDatabase::removeDatabase("save_connection");

    // [개선 1: Atomic Save 마무리] 임시 파일 저장이 완벽하게 완료되었으므로, 기존 파일을 덮어씌웁니다.
    QFile originalFile(filePath);
    if (originalFile.exists()) {
        if (!originalFile.remove()) {
  //          qDebug() << "FileManager: Failed to remove old original file!";
            return false;
        }
    }

    // 임시 파일을 원본 파일명으로 변경
    if (!QFile::rename(tempFilePath, filePath)) {
  //      qDebug() << "FileManager: Failed to rename temp file to original path!";
        return false;
    }

    // // [신규 추가] 저장이 완료되었으므로 Undo Stack을 Clean 상태로 만듭니다.
    // if (m_ctx && m_ctx->undoStack()) {
    //     m_ctx->undoStack()->setClean();
    // }
    addRecentFile(filePath);
    return true;
}

bool FileManager::loadDrawing(const QString &filePath, MyGraphicsScene *scene,
                              bool asTemplate, QProgressDialog *progress) {
    if (!scene || !m_ctx || !m_ctx->entityFactory() || !m_ctx->layerManager())
        return false;

    //  파일이 존재하지 않으면 즉시 리턴하여 빈 도면 생성을 방지합니다.
    if (!QFile::exists(filePath)) {
 //       qDebug() << "[FileManager] loadDrawing - File does not exist:" << filePath;
        return false;
    }

    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "load_connection");
        db.setDatabaseName(filePath);
        if (!db.open()) {
//            qDebug() << "[FileManager] Failed to open DB for loading:" << filePath;
            return false;
        }

//        qDebug() << "[FileManager] Loading drawing:" << filePath;
        scene->clearAll();

        // 1. 레이어 복원
        QSqlQuery layerQuery("SELECT * FROM layers", db);
        // [출력 스타일] print_color 컬럼이 생기기 전에 저장된 옛 .cdb 파일에는
        // 이 컬럼이 없다. loadDrawing()은 스키마 마이그레이션을 수행하지 않으므로
        // 컬럼 존재 여부를 먼저 확인해야 한다. (없는 컬럼을 value()로 조회하면
        // 레이어 수만큼 "QSqlQuery::value: unknown field name 'print_color'" 경고가 출력됨)
        const bool hasPrintColorColumn = layerQuery.record().contains("print_color");
        while (layerQuery.next()) {
            QString name = layerQuery.value("name").toString();
            QColor color(layerQuery.value("color").toString());
            double weight = layerQuery.value("weight").toDouble();
            QString linetype = layerQuery.value("linetype").toString();

            Layer newLayer(name, color, weight, linetype);
            newLayer.setLayerOn(layerQuery.value("is_on").toInt() == 1);
            newLayer.setPlotOn(layerQuery.value("is_plot").toInt() == 1);
            newLayer.setLayerLock(layerQuery.value("is_lock").toInt() == 1);

            // [출력 스타일] 옛 파일(컬럼 없음)은 조회를 건너뛰어 "오버라이드 없음"으로 둔다.
            if (hasPrintColorColumn) {
                QString printColorStr = layerQuery.value("print_color").toString();
                if (!printColorStr.isEmpty()) {
                    newLayer.setPrintColor(QColor(printColorStr));
                }
            }

            // 이미 존재하면 업데이트, 없으면 추가
            if (m_ctx->layerManager()->getLayer(name)) {
                m_ctx->layerManager()->editLayer(name, newLayer);
            } else {
                m_ctx->layerManager()->addLayer(newLayer);
            }
        }

        QSqlQuery query("SELECT e.id, e.type, e.layer, e.properties, g.data "
                        "FROM entities e JOIN geometry g ON e.id = g.entity_id",
                        db);

  //      int loadedCount = 0;
        int entityCount = 0;
        if (progress)
            progress->setLabelText(tr("도형 데이터를 불러오는 중입니다..."));
        while (query.next()) {
 //           loadedCount++;
            // [응답 없음 방지] 엔티티가 많은 큰 도면은 이 루프가 오래 걸려
            // UI 스레드가 통째로 막힌다. DXF/DWG 가져오기와 동일하게 일정
            // 간격마다 진행률 다이얼로그를 갱신해 이벤트 루프를 펌핑한다
            // (QProgressDialog::setValue가 내부적으로 processEvents를 호출).
            if (progress && (++entityCount % 32 == 0)) {
                progress->setLabelText(
                    tr("도형 데이터를 불러오는 중입니다... (%1개)").arg(entityCount));
                progress->setValue(progress->value() + 1);
            }
            QString type = query.value("type").toString();
            QString layerName = query.value("layer").toString();
            QByteArray propData = query.value("properties").toByteArray();
            QByteArray geoData = query.value("data").toByteArray();

            QJsonDocument propDoc = QJsonDocument::fromJson(propData);
            QJsonObject propObj = propDoc.object();

            Layer *layer = m_ctx->layerManager()->getLayer(layerName);
            QPen pen;
            if (layer) {
                pen.setColor(layer->color());
                pen.setWidthF(layer->weight());
                QString ltName = layer->linetype();
                if (ltName != "Continuous" && m_ctx->lineTypeManager()) {
                    QVector<qreal> pattern =
                        m_ctx->lineTypeManager()->getQtDashPattern(ltName);
                    if (!pattern.isEmpty()) {
                        pen.setStyle(Qt::CustomDashLine);
                        pen.setDashPattern(pattern);
                    }
                }
            } else {
                pen.setColor(QColor(propObj["color"].toString()));
                pen.setWidthF(propObj["weight"].toDouble());
            }
            pen.setCosmetic(true);

            QDataStream in(&geoData, QIODevice::ReadOnly);
            in.setVersion(QDataStream::Qt_6_0); // 저장할 때와 동일한 버전 사용

            // 역직렬화 함수 호출
            CadItem *cadItem = deserializeCadItem(type, propObj, in, pen);

            if (cadItem) {
                cadItem->setLayerName(layerName);
                cadItem->setGripsVisible(false);
                scene->reparentItemToLayer(cadItem, layerName);
            } else {
 //               qDebug() << "[FileManager] Failed to load item of type:" << type;
            }
        }
 //       qDebug() << "[FileManager] Load complete. Items count:" << loadedCount;

        QSqlQuery metaQuery("SELECT key, value FROM file_metadata WHERE key IN "
                            "('current_layer', 'text_layer', 'dimension_style', "
                            "'drawing_scale', 'drawing_size_name', 'drawing_limits', "
                            "'multiline_setup', 'theme', 'symbol_registry', "
                            "'doc_settings')",
                            db);
        QByteArray legacyMultilineJson; // [3-3] 옛 multiline_setup 캡처용
        while (metaQuery.next()) {
            QString key = metaQuery.value(0).toString();
            QByteArray valData = metaQuery.value(1).toByteArray();
            QString val = QString::fromUtf8(valData);
            if (key == "current_layer") {
                m_ctx->layerManager()->setCurrentLayer(val);
            } else if (key == "text_layer") {
                m_ctx->setTextLayerName(val);
            } else if (key == "dimension_style") {
                QJsonDocument doc = QJsonDocument::fromJson(valData);
                scene->setDimensionStyle(DimensionStyle::fromJson(doc.object()));
            } else if (key == "drawing_scale") {
                scene->setDrawingScale(val.toDouble());
            } else if (key == "drawing_size_name") {
                scene->setDrawingSizeName(val);
            } else if (key == "drawing_limits") {
                QJsonDocument doc = QJsonDocument::fromJson(valData);
                QJsonObject obj = doc.object();
                scene->setDrawingLimits(QRectF(obj["x"].toDouble(), obj["y"].toDouble(),
                                               obj["w"].toDouble(), obj["h"].toDouble()));
            } else if (key == "multiline_setup") {
                // [3-3] 레거시 키. 여기선 캡처만 하고 루프 종료 후 doc_settings 로
                //   이관한다(doc_settings 가 우선, 없는 키만 채움).
                legacyMultilineJson = valData;
            } else if (key == "theme") {
                // 템플릿으로 여는 경우: 템플릿에 박제된 테마로 사용자 화면을
                // 갈아엎지 않는다 (라이트 작업자에게 다크 템플릿이 강제되는 상황 방지).
                if (!asTemplate && m_ctx->themeManager()) {
                    int themeVal = val.toInt();
                    m_ctx->themeManager()->setTheme(static_cast<ThemeManager::ThemeType>(themeVal));
                }
            } else if (key == "symbol_registry") {
                QJsonDocument doc = QJsonDocument::fromJson(valData);
                QJsonObject regObj = doc.object();
                for (auto it = regObj.constBegin(); it != regObj.constEnd(); ++it) {
                    QJsonObject infoObj = it.value().toObject();
                    SymbolInfo info;
                    info.type = infoObj["type"].toString();
                    info.name = infoObj["name"].toString();
                    QJsonObject propsObj = infoObj["props"].toObject();
                    for (auto pit = propsObj.constBegin(); pit != propsObj.constEnd(); ++pit)
                        info.props.insert(pit.key(), pit.value().toVariant());
                    scene->symbolRegistry()->registerSymbol(it.key(), info);
                }
            } else if (key == "doc_settings") {
                // [3-1] 도면 스코프 도구 기본값 복원. clearAll()에서 이미 비워졌으므로
                //   이 키가 없는 옛 파일은 빈 상태로 남는다(전역/코드 기본값 사용).
                QJsonDocument doc = QJsonDocument::fromJson(valData);
                scene->documentSettings()->fromJson(doc.object());
            }
        }

        // [3-3] 레거시 multiline_setup → doc_settings("TwoLineTool/*") 이관.
        //   doc_settings 가 이미 채운 키는 건드리지 않는다(신 포맷 우선).
        if (!legacyMultilineJson.isEmpty()) {
            const QJsonObject ml =
                QJsonDocument::fromJson(legacyMultilineJson).object();
            DocumentSettings *ds = scene->documentSettings();
            for (auto it = ml.constBegin(); it != ml.constEnd(); ++it) {
                const QString docKey =
                    QStringLiteral("TwoLineTool/") + it.key();
                if (!ds->contains(docKey))
                    ds->setValue(docKey, it.value().toVariant());
            }
        }
        // 방금 불러온 이 도면의 값으로 TwoLineTool 전역 정적을 즉시 동기화한다
        //   (Setup 명령이 도구 활성화 전에 호출돼도 올바른 값을 보이도록).
        TwoLineTool::loadDocDefaults(scene);

        db.close();
    }
    QSqlDatabase::removeDatabase("load_connection");

    // // [신규 추가] 도면을 방금 불러왔으므로 Undo Stack을 비우고 Clean 상태로 만듭니다.
    // if (m_ctx && m_ctx->undoStack()) {
    //     m_ctx->undoStack()->clear();
    // }
    // 템플릿은 "새 도면의 씨앗"일 뿐이므로 최근 파일 목록에 남기지 않는다.
    if (!asTemplate)
        addRecentFile(filePath);
    return true;
}

QByteArray FileManager::capturePreview(MyGraphicsScene *scene) {
//    qDebug() << "FileManager: capturePreview - START";
    if (!scene) {
//        qDebug() << "FileManager: capturePreview - Scene is NULL!";
        return QByteArray();
    }

//    qDebug() << "FileManager: capturePreview - Calculating itemsBoundingRect...";
    QRectF targetRect = scene->itemsBoundingRect();
    // qDebug() << "FileManager: capturePreview - targetRect (Block BBox):"
    //          << targetRect << "Aspect Ratio:"
    //          << (targetRect.height() != 0
    //                  ? targetRect.width() / targetRect.height()
    //                  : 0);

    if (targetRect.isEmpty() || !targetRect.isValid()) {
        // qDebug() << "FileManager: capturePreview - targetRect is EMPTY or INVALID, "
        //             "using default.";
        targetRect = QRectF(-10, -10, 20, 20);
    }

    // [수정] 종횡비 유지 로직 추가
    double w = targetRect.width();
    double h = targetRect.height();
    int maxDim = 512;
    QSize imgSize(maxDim, maxDim);

    if (w > 0 && h > 0) {
        if (w > h) {
            imgSize.setHeight(qMax(1, static_cast<int>(maxDim * (h / w))));
        } else {
            imgSize.setWidth(qMax(1, static_cast<int>(maxDim * (w / h))));
        }
    }

//    qDebug() << "FileManager: capturePreview - Creating QImage" << imgSize;
    QImage image(imgSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent); // [수정] 배경 투명화

 //   qDebug() << "FileManager: capturePreview - Creating QPainter scope";
    {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);

//        qDebug() << "FileManager: capturePreview - Calling scene->render";
        try {
            // [보정] 썸네일에서 점선(Dashed)이 너무 듬성듬성하여 안 보일 수 있으므로
            // 강제로 실선으로 그림 다만 원본 객체의 속성을 훼손하지 않기 위해 원본
            // 펜을 백업 후 복구합니다.
            QMap<CadItem *, QPen> penBackup;
            QList<QGraphicsItem*> sceneItems = scene->items();
            for (int si = 0; si < sceneItems.size(); ++si) {
                if (CadItem *cadItem = dynamic_cast<CadItem *>(sceneItems[si])) {
                    QPen p = cadItem->pen();
                    if (p.style() != Qt::SolidLine) {
                        penBackup.insert(cadItem, p);
                        p.setStyle(Qt::SolidLine);
                        cadItem->setPen(p);
                    }
                }
            }

            scene->render(&painter, image.rect(), targetRect);

            // 원본 펜 복구
            for (auto it = penBackup.begin(); it != penBackup.end(); ++it) {
                it.key()->setPen(it.value());
            }
        } catch (...) {
            // qDebug()
            // << "FileManager: CRITICAL - Crash/Exception during scene->render!";
        }
    } // painter destroyed here

//    qDebug() << "FileManager: capturePreview - Flipping image for standard view";
    image = image.flipped(Qt::Vertical); // CAD(Y-up) -> Image(Y-down) 보정

//    qDebug() << "FileManager: capturePreview - Saving to PNG buffer";
    QByteArray ba;
    {
        QBuffer buffer(&ba);
        if (buffer.open(QIODevice::WriteOnly)) {
            image.save(&buffer, "PNG");
            buffer.close();
        }
    }
 //   qDebug() << "FileManager: capturePreview - DONE. Buffer size:" << ba.size();
    return ba;
}

void FileManager::addRecentFile(const QString &filePath) {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    QStringList files = settings.value("recentFileList").toStringList();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > 10)
        files.removeLast();
    settings.setValue("recentFileList", files);
}

void FileManager::removeRecentFile(const QString &filePath) {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    QStringList files = settings.value("recentFileList").toStringList();
    files.removeAll(filePath);
    settings.setValue("recentFileList", files);
}

void FileManager::clearRecentFiles() {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    settings.remove("recentFileList");
}

QStringList FileManager::getRecentFiles() const {
    QSettings settings; // [3-0] 정규 루트 (Autodraft/Autodraft)
    return settings.value("recentFileList").toStringList();
}


QImage FileManager::loadThumbnail(const QString &filePath) {
    if (!QFile::exists(filePath)) return QImage();

    // 고유한 DB 커넥션 이름 생성
    QString connectionName = QUuid::createUuid().toString();
    QImage image;

    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connectionName);
        db.setDatabaseName(filePath);
        if (db.open()) {
            QSqlQuery query("SELECT value FROM file_metadata WHERE key = 'preview'", db);
            if (query.next()) {
                image.loadFromData(query.value(0).toByteArray(), "PNG");
            }
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName);
    return image;
}

bool FileManager::exportSelectedItems(const QString &filePath,
                                      const QList<QGraphicsItem *> &items,
                                      QPointF basePoint) {
    if (items.isEmpty() || !m_ctx)
        return false;

 //   qDebug() << "FileManager: exportSelectedItems START. Path:" << filePath;

    // 1. 미리보기 데이터 생성 (임시 씬 활용, DB 연결 전 독립 수행)
    QByteArray previewData;
    {
        try {
            // qDebug() << "FileManager: Generating preview... Item count:"
            //          << items.size();
            MyGraphicsScene tempScene;
            tempScene.setSceneRect(-100000, -100000, 200000, 200000);
            bool hasItems = false;
            for (int ii = 0; ii < items.size(); ++ii) {
                QGraphicsItem *item = items[ii];
                if (item && item->scene() && dynamic_cast<CadItem *>(item)) {
                    CadItem *cadItem = static_cast<CadItem *>(item);
                    CadItem *clone = cadItem->clone();
                    if (clone) {
                        clone->setGripsVisible(false);
                        // [수정] 프리뷰 이미지도 원점(0,0)을 기준으로 생성되도록 이동
                        clone->move(MyVector(-basePoint));
                        tempScene.addItem(clone);
                        hasItems = true;
                    }
                }
            }
            if (hasItems) {
                previewData = capturePreview(&tempScene);
            }
            // qDebug() << "FileManager: Preview generation phase finished. Data size:"
            //          << previewData.size();
        } catch (...) {
            // qDebug() << "FileManager: CRITICAL - Crash during Preview Generation "
            //             "Phase!";
        }
    }

    // 2. DB 초기화 및 저장 시작
    if (!initDatabase(filePath))
        return false;

    {
        QSqlDatabase db = QSqlDatabase::database("save_connection");
        if (!db.isOpen())
            return false;

        if (!db.transaction())
            return false;

        QSqlQuery query(db);
        query.exec("DELETE FROM layers");
        query.exec("DELETE FROM entities");
        query.exec("DELETE FROM geometry");
        query.exec("DELETE FROM file_metadata");

        // 미리보기 저장
        if (!previewData.isEmpty()) {
            query.prepare(
                "INSERT INTO file_metadata (key, value) VALUES ('preview', ?)");
            query.addBindValue(previewData);
            query.exec();
        }

        QSet<QString> usedLayerNames;
        for (int ii = 0; ii < items.size(); ++ii) {
            if (CadItem *cadItem = dynamic_cast<CadItem *>(items[ii])) {
                usedLayerNames.insert(cadItem->layerName());
            }
        }

        // 1. 필요한 레이어만 저장
        if (m_ctx->layerManager()) {
            QList<Layer> allLayers = m_ctx->layerManager()->getAllLayers();
            for (int li = 0; li < allLayers.size(); ++li) {
                const Layer &layer = allLayers[li];
                if (usedLayerNames.contains(layer.name())) {
                    query.prepare(
                        "INSERT INTO layers (name, color, weight, linetype, is_on, "
                        "is_plot, is_lock, print_color) "
                        "VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
                    query.addBindValue(layer.name());
                    query.addBindValue(layer.color().name());
                    query.addBindValue(layer.weight());
                    query.addBindValue(layer.linetype());
                    query.addBindValue(layer.isLayerOn() ? 1 : 0);
                    query.addBindValue(layer.isPlotOn() ? 1 : 0);
                    query.addBindValue(layer.isLayerLock() ? 1 : 0);
                    query.addBindValue(layer.hasPrintColorOverride() ? layer.printColor().name() : QString());
                    query.exec();
                }
            }
        }

        // 2. 엔티티 저장 (기준점 오프셋 적용)
        for (int ii = 0; ii < items.size(); ++ii) {
            QGraphicsItem *item = items[ii];
            // [수정] 블록 내부 객체만 제외
            if (dynamic_cast<BlockItem *>(item->parentItem()) != nullptr)
                continue;

            CadItem *cadItem = dynamic_cast<CadItem *>(item);
            if (!cadItem)
                continue;

            QJsonObject propObj;
            QByteArray geoData;
            QDataStream out(&geoData, QIODevice::WriteOnly);
            out.setVersion(QDataStream::Qt_6_0); // 예: 현재 사용하는 Qt 메이저.마이너 버전으로 고정

            // 직렬화 함수 호출
            serializeCadItem(cadItem, out, propObj, basePoint);

            QString type = propObj["Type"].toString();
            QString layer = cadItem->layerName();
            QJsonDocument propDoc(propObj);

            query.prepare(
                "INSERT INTO entities (type, layer, properties) VALUES (?, ?, ?)");
            query.addBindValue(type);
            query.addBindValue(layer);
            query.addBindValue(QString(propDoc.toJson(QJsonDocument::Compact)));

            if (query.exec()) {
                qint64 lastId = query.lastInsertId().toLongLong();
                QSqlQuery geoQuery(db);
                geoQuery.prepare(
                    "INSERT INTO geometry (entity_id, data) VALUES (?, ?)");
                geoQuery.addBindValue(lastId);
                geoQuery.addBindValue(geoData);
                geoQuery.exec();
            }
        }

        // [신규] 현재 레이어 정보도 메타데이터로 저장 (Save와 동일하게)
        if (m_ctx->layerManager()) {
            Layer *current = m_ctx->layerManager()->getCurrentLayer();
            if (current) {
                query.prepare("INSERT INTO file_metadata (key, value) VALUES "
                              "('current_layer', ?)");
                query.addBindValue(current->name().toUtf8());
                query.exec();
            }
        }

        // 테마 정보 저장
        if (m_ctx->themeManager()) {
            int themeVal = static_cast<int>(m_ctx->themeManager()->currentTheme());
            query.prepare("INSERT INTO file_metadata (key, value) VALUES "
                          "('theme', ?)");
            query.addBindValue(QString::number(themeVal).toUtf8());
            query.exec();
        }

        // [신규] 드로잉 설정 저장 (축척, 크기, Limits) - 원본 씬에서 가져옴
        if (!items.isEmpty() && items.first()->scene()) {
            MyGraphicsScene *origScene = dynamic_cast<MyGraphicsScene *>(items.first()->scene());
            if (origScene) {
                query.prepare("INSERT INTO file_metadata (key, value) VALUES ('drawing_scale', ?)");
                query.addBindValue(QString::number(origScene->drawingScale()));
                query.exec();

                query.prepare("INSERT INTO file_metadata (key, value) VALUES ('drawing_size_name', ?)");
                query.addBindValue(origScene->drawingSizeName().toUtf8());
                query.exec();

                QRectF limits = origScene->drawingLimits();
                QJsonObject limitsObj;
                limitsObj.insert("x", limits.x());
                limitsObj.insert("y", limits.y());
                limitsObj.insert("w", limits.width());
                limitsObj.insert("h", limits.height());
                QJsonDocument limitsDoc(limitsObj);
                query.prepare("INSERT INTO file_metadata (key, value) VALUES ('drawing_limits', ?)");
                query.addBindValue(limitsDoc.toJson(QJsonDocument::Compact));
                query.exec();
            }
        }

        db.commit();
        db.close();
    }
    QSqlDatabase::removeDatabase("save_connection");

    emit libraryChanged(); // [추가] 라이브러리 변경 알림
    return true;
}

QList<CadItem *> FileManager::importBlock(const QString &filePath,
                                          QString *originalLayerName) {
    QList<CadItem *> items;
    if (!m_ctx || !m_ctx->entityFactory() || !m_ctx->layerManager())
        return items;

    {
        QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", "import_connection");
        db.setDatabaseName(filePath);
        if (!db.open()) {
//            qDebug() << "[FileManager] Failed to open DB for importing:" << filePath;
            QSqlDatabase::removeDatabase("import_connection");
            return items;
        }

        // [수정] 블록의 '기준 레이어' 정보 가져오기
        QSqlQuery metaQuery(
            "SELECT value FROM file_metadata WHERE key = 'current_layer'", db);
        if (metaQuery.next()) {
            QString layerStr = QString::fromUtf8(metaQuery.value(0).toByteArray());
            if (originalLayerName) {
                *originalLayerName = layerStr;
            }
        }

        // 1. 레이어 확인/생성
        QSqlQuery layerQuery("SELECT * FROM layers", db);
        while (layerQuery.next()) {
            QString name = layerQuery.value("name").toString();
            if (!m_ctx->layerManager()->getLayer(name)) {
                QColor color(layerQuery.value("color").toString());
                double weight = layerQuery.value("weight").toDouble();
                QString linetype = layerQuery.value("linetype").toString();
                Layer newLayer(name, color, weight, linetype);
                m_ctx->layerManager()->addLayer(newLayer);
            }
        }

        // 2. 엔티티 로드
        QSqlQuery query("SELECT e.type, e.layer, e.properties, g.data "
                        "FROM entities e JOIN geometry g ON e.id = g.entity_id",
                        db);

        while (query.next()) {
            QString type = query.value("type").toString();
            QString layerName = query.value("layer").toString();
            QByteArray propData = query.value("properties").toByteArray();
            QByteArray geoData = query.value("data").toByteArray();

            QJsonDocument propDoc = QJsonDocument::fromJson(propData);
            QJsonObject propObj = propDoc.object();

            Layer *layer = m_ctx->layerManager()->getLayer(layerName);
            QPen pen;
            if (layer) {
                pen.setColor(layer->color());
                pen.setWidthF(layer->weight());
                // [신규] 레이어의 라인타입 속성(점선 등) 반영
                if (m_ctx->lineTypeManager()) {
                    QVector<qreal> pattern =
                        m_ctx->lineTypeManager()->getQtDashPattern(layer->linetype());
                    if (!pattern.isEmpty()) {
                        pen.setStyle(Qt::CustomDashLine);
                        pen.setDashPattern(pattern);
                    } else {
                        pen.setStyle(Qt::SolidLine);
                    }
                }
            } else {
                pen.setColor(QColor(propObj["color"].toString()));
                pen.setWidthF(propObj["weight"].toDouble());
            }
            pen.setCosmetic(true);

            QDataStream in(&geoData, QIODevice::ReadOnly);
            in.setVersion(QDataStream::Qt_6_0); // 저장할 때와 동일한 버전 사용

            // 역직렬화 함수 호출
            CadItem *cadItem = deserializeCadItem(type, propObj, in, pen);

            if (cadItem) {
                cadItem->setLayerName(layerName);
                cadItem->setGripsVisible(false);
                items.append(cadItem);
            }
        }
        db.close();
    }
    QSqlDatabase::removeDatabase("import_connection");
    return items;
}

// ─────────────────────────────────────────────────────────
// 공통 직렬화 함수 (이곳에 GDT와 Datum의 속성을 정의합니다)
// ─────────────────────────────────────────────────────────
void FileManager::serializeCadItem(CadItem *item, QDataStream &out,
                                   QJsonObject &propObj, QPointF offset) {
    QMap<QString, QVariant> props = item->getProperties();
    QString type = props.take("Type").toString();
    QPen pen = item->pen();

    for (auto it = props.begin(); it != props.end(); ++it) {
        propObj.insert(it.key(), QJsonValue::fromVariant(it.value()));
    }
    propObj.insert("color", pen.color().name());
    propObj.insert("weight", pen.widthF());
    propObj.insert("Type", type);

    // [상세도] role/label 태그를 모든 타입에서 보존 → 재로드 후에도 소속 구분 유지
    {
        const QVariant drRole = item->data(DETAIL_REGION_ROLE);
        if (drRole.isValid() && drRole.toInt() != 0)
            propObj.insert("detailRegionRole", drRole.toInt());
        const QString drLabel = item->data(DETAIL_REGION_LABEL).toString();
        if (!drLabel.isEmpty())
            propObj.insert("detailRegionLabel", drLabel);
    }

    if (type == "Line") {
        out << props["StartX"].toDouble() - offset.x()
        << props["StartY"].toDouble() - offset.y()
        << props["EndX"].toDouble() - offset.x()
        << props["EndY"].toDouble() - offset.y();
        // 다중선 그룹 태그 (QGraphicsItem::data) → JSON 프로퍼티로 보존
        const QString mlGid     = item->data(ML_DATA_GROUP_ID).toString();
        const QString mlRole    = item->data(ML_DATA_ROLE).toString();
        const double  mlSpacing = item->data(ML_DATA_SPACING).toDouble();
        if (!mlGid.isEmpty())    propObj.insert("mlGroupId",  mlGid);
        if (!mlRole.isEmpty())   propObj.insert("mlRole",     mlRole);
        if (mlSpacing > 0.0)     propObj.insert("mlSpacing",  mlSpacing);
        // 도어/창문 그룹 태그 보존
        const QString pdGid   = item->data(PD_DATA_GROUP_ID).toString();
        const QString pdCLine = item->data(PD_DATA_CLOSING_LINE).toString();
        if (!pdGid.isEmpty())   propObj.insert("pdGroupId",    pdGid);
        if (!pdCLine.isEmpty()) propObj.insert("pdClosingLine", pdCLine);
        // [추가] 구멍(Hole) 중심선 그룹 태그 보존 — 저장/로드 후에도 나사규격 등을
        // 바꿀 때 이 중심선을 찾아 길이를 다시 맞출 수 있도록 유지한다.
        const QString holeGid = item->data(HOLE_DATA_GROUP_ID).toString();
        if (!holeGid.isEmpty()) propObj.insert("holeGroupId", holeGid);
        // [추가] 구멍 단면(HoleSectionTool) 그룹 태그 + 재생성용 배치 정보 보존
        const QString holeSecGid = item->data(HOLE_SECTION_GROUP_ID).toString();
        if (!holeSecGid.isEmpty()) {
            propObj.insert("holeSectionGroupId", holeSecGid);
            // topX/Y는 절대 좌표라 다른 좌표들과 동일하게 offset을 반영하고,
            // dirX/Y는 방향 벡터(델타)라 offset과 무관하게 그대로 저장한다.
            propObj.insert("holeSectionTopX", item->data(HOLE_SECTION_TOP_X).toDouble() - offset.x());
            propObj.insert("holeSectionTopY", item->data(HOLE_SECTION_TOP_Y).toDouble() - offset.y());
            propObj.insert("holeSectionDirX", item->data(HOLE_SECTION_DIR_X).toDouble());
            propObj.insert("holeSectionDirY", item->data(HOLE_SECTION_DIR_Y).toDouble());
            propObj.insert("holeSectionThickness", item->data(HOLE_SECTION_MATERIAL_THICKNESS).toDouble());
        }
    } else if (type == "Circle") {
        out << props["CenterX"].toDouble() - offset.x()
        << props["CenterY"].toDouble() - offset.y()
        << props["Radius"].toDouble();
        // 상세도 경계원 태그 보존 (그 안에서 그리는 치수의 measureScale 자동 적용용)
        {
            const QVariant drScale = item->data(DETAIL_REGION_SCALE);
            if (drScale.isValid() && drScale.toDouble() > 0.0)
                propObj.insert("detailRegionScale", drScale.toDouble());
            const QString drLabel = item->data(DETAIL_REGION_LABEL).toString();
            if (!drLabel.isEmpty())
                propObj.insert("detailRegionLabel", drLabel);
        }
    } else if (type == "Arc") {
        out << props["CenterX"].toDouble() - offset.x()
        << props["CenterY"].toDouble() - offset.y()
        << props["Radius"].toDouble() << props["StartAngle"].toDouble()
        << props["SpanAngle"].toDouble();
        // 도어/창문 그룹 태그 보존
        {
            const QString pdGid = item->data(PD_DATA_GROUP_ID).toString();
            if (!pdGid.isEmpty()) propObj.insert("pdGroupId", pdGid);
        }
        // 상세도 경계선(경계선 모드 마커 arc)의 라벨 태그 보존
        {
            const QString drLabel = item->data(DETAIL_REGION_LABEL).toString();
            if (!drLabel.isEmpty()) propObj.insert("detailRegionLabel", drLabel);
        }
    } else if (type == "Polyline") {
        PolylineItem *polyItem = dynamic_cast<PolylineItem *>(item);
        if (polyItem) {
            MyPolyline poly = polyItem->getPolyline();
            out << (bool)poly.isClosed;
            out << (quint32)poly.vertices.size();
            for (const auto &v : poly.vertices) {
                out << v.x - offset.x() << v.y - offset.y();
            }
        }
        // 도어/창문 그룹 태그 보존
        {
            const QString pdGid = item->data(PD_DATA_GROUP_ID).toString();
            if (!pdGid.isEmpty()) propObj.insert("pdGroupId", pdGid);
        }
    } else if (type == "Text") {
        out << item->pos().x() - offset.x() << item->pos().y() - offset.y();
        if (!propObj.contains("content")) {
            propObj.insert("content", QJsonValue::fromVariant(props["Content"]));
            propObj.insert("fontFamily",
                           QJsonValue::fromVariant(props["FontFamily"]));
            propObj.insert("fontSize", QJsonValue::fromVariant(props["FontSize"]));
            propObj.insert("fontBold", QJsonValue::fromVariant(props["FontBold"]));
            propObj.insert("fontItalic", QJsonValue::fromVariant(props["FontItalic"]));
            propObj.insert("fontUnderline", QJsonValue::fromVariant(props["FontUnderline"]));
            propObj.insert("rotation", QJsonValue::fromVariant(props["Rotation"]));
            propObj.insert("isAttribute", QJsonValue::fromVariant(props["isAttribute"]));
            propObj.insert("tag", QJsonValue::fromVariant(props["tag"]));
            propObj.insert("prompt", QJsonValue::fromVariant(props["prompt"]));
        }
    } else if (type == "Block") {
        BlockItem *block = dynamic_cast<BlockItem *>(item);
        if (block) {
            out << block->blockName();
            out << block->blockScaleX();
            out << block->blockScaleY();
            out << block->blockRotation();
            out << block->insertionPoint().x - offset.x()
                << block->insertionPoint().y - offset.y();
            QList<CadItem *> children = block->getItems();
            out << (quint32)children.size();

            for (CadItem *child : std::as_const(children)) {
                QByteArray childData;
                QDataStream childOut(&childData, QIODevice::WriteOnly);
                QJsonObject childProps;
                serializeCadItem(child, childOut, childProps, QPointF(0, 0));

                out << childProps["Type"].toString();
                out << child->layerName();
                QJsonDocument doc(childProps);
                out << doc.toJson(QJsonDocument::Compact);
                out << childData;
            }
        }
    } else if (type == "Dimension") {
        out << props["StartX"].toDouble() - offset.x()
        << props["StartY"].toDouble() - offset.y()
        << props["EndX"].toDouble() - offset.x()
        << props["EndY"].toDouble() - offset.y()
        << props["DimLineX"].toDouble() - offset.x()
        << props["DimLineY"].toDouble() - offset.y();
        
        // [신규] 각도 치수(Angular)일 경우 두 번째 선 끝점(End2)도 바이너리 저장
        if (props["DimType"].toString() == "Angular") {
            out << props["End2X"].toDouble() - offset.x()
                << props["End2Y"].toDouble() - offset.y();
        }

        // [수정] 원본 텍스트(m_data.text)를 우선적으로 저장하여 '자동 치수' 상태 유지
        propObj.insert("content", QJsonValue::fromVariant(props["Text"]));
    } else if (type == "Hatch") {
        QPainterPath path = item->geometryPath();
        if (!offset.isNull()) {
            QTransform t;
            t.translate(-offset.x(), -offset.y());
            path = t.map(path);
        }
        out << path;
        // patternName, scale, angle을 바이너리 스트림에도 저장 (JSON 파싱 오류 방지)
        QString patternName = propObj["Pattern"].toString();
        double patternScale = propObj["Scale"].toDouble();
        double patternAngle = propObj["Angle"].toDouble();
        out << patternName << patternScale << patternAngle;

        // ── [신규 통합] 기하공차 및 데이텀 직렬화 ──
    } else if (type == "Gdt") {
        GdtItem *gdtItem = dynamic_cast<GdtItem *>(item);
        if (gdtItem) {
            const GdtData &d = gdtItem->gdtData();
            out << (d.position.x - offset.x())
                << (d.position.y - offset.y())
                << (qint32)d.symbol
                << d.toleranceValue
                << d.hasDiameter
                << d.textHeight;
            // 데이텀 목록
            out << (quint32)d.datumRefs.size();
            for (int i = 0; i < d.datumRefs.size(); ++i)
                out << d.datumRefs[i];

            // 지시선 및 회전 저장
            out << d.rotation << d.useLeader;
            out << (quint32)d.leaderPoints.size();
            for (int i = 0; i < d.leaderPoints.size(); ++i) {
                out << (d.leaderPoints[i].x - offset.x())
                << (d.leaderPoints[i].y - offset.y());
            }
        }
    } else if (type == "Datum") {
        DatumItem *datumItem = dynamic_cast<DatumItem *>(item);
        if (datumItem) {
            const DatumData &d = datumItem->datumData();
            out << d.text
                << (d.startPoint.x - offset.x())
                << (d.startPoint.y - offset.y())
                << (d.endPoint.x - offset.x())
                << (d.endPoint.y - offset.y())
                << d.textHeight;

            // [신규] 다중 지시선 저장
            out << (quint32)d.leaderPoints.size();
            for (int i = 0; i < d.leaderPoints.size(); ++i) {
                out << (d.leaderPoints[i].x - offset.x())
                    << (d.leaderPoints[i].y - offset.y());
            }
        }
    } else if (type == "SurfaceFinish") {
        SurfaceFinishItem *sfItem = dynamic_cast<SurfaceFinishItem *>(item);
        if (sfItem) {
            const SurfaceFinishData &d = sfItem->surfaceFinishData();
            out << (d.position.x - offset.x())
                << (d.position.y - offset.y())
                << (qint32)d.symbol
                << d.raValue
                << d.productionMethod
                << d.cutoffValue
                << d.laySymbol
                << d.machiningAllowance
                << (qint32)d.legacyGrade
                << d.showTailBar
                << d.textHeight
                << d.rotation
                << d.useLeader;

            out << (quint32)d.leaderPoints.size();
            for (int i = 0; i < d.leaderPoints.size(); ++i) {
                out << (d.leaderPoints[i].x - offset.x())
                    << (d.leaderPoints[i].y - offset.y());
            }

            // [신규] W/X/Y/Z 약식 등급 문자 (기존 저장 형식과의 호환을 위해 끝에 추가)
            out << d.raLetterCode;

            // [신규] 기호/문자 전용 레이어 (기존 저장 형식과의 호환을 위해 끝에 추가)
            out << d.symbolLayer << d.textLayer;
        }
    } else if (type == "Spline") {
        SplineItem *spline = dynamic_cast<SplineItem *>(item);
        if (spline) {
            const MySpline *s = dynamic_cast<const MySpline *>(spline->getEntityData());
            if (s) {
                out << (qint32)s->degree << (bool)s->isClosed;
                out << (quint32)s->controlPoints.size();
                for (size_t i = 0; i < s->controlPoints.size(); ++i) {
                    out << s->controlPoints[i].x - offset.x() << s->controlPoints[i].y - offset.y();
                }
                out << (quint32)s->knots.size();
                for (size_t i = 0; i < s->knots.size(); ++i) {
                    out << s->knots[i];
                }
            }
        }
    } else if (type == "Ellipse") {
        EllipseItem *ellipseItem = dynamic_cast<EllipseItem *>(item);
        if (ellipseItem) {
            const MyEllipse e = ellipseItem->getEllipse();
            out << (e.center.x - offset.x()) << (e.center.y - offset.y())
                << e.majorAxis.x << e.majorAxis.y // 중심 기준 벡터라 offset 무관
                << e.ratio << e.startParam << e.endParam;
        }
    } else if (type == "Hole") {
        HoleItem *holeItem = dynamic_cast<HoleItem *>(item);
        if (holeItem) {
            const MyHole &d = holeItem->holeData();
            out << (d.position.x - offset.x())
                << (d.position.y - offset.y())
                << d.diameter
                << d.throughHole
                << d.depth
                << d.isThreaded
                << d.threadSpec
                << d.threadClass
                << d.threadDepth
                << d.toleranceClass
                << d.hasCounterbore
                << d.cbDiameter
                << d.cbDepth
                << d.hasCountersink
                << d.csDiameter
                << d.csAngle
                << (qint32)d.symbolStyle
                << d.showCenterLine
                << d.baseLayer
                << d.threadLayer
                << d.cbLayer
                << d.csLayer
                << (qint32)d.renderMode; // [신규] 반원 표시 모드 (기존 저장 형식과의 호환을 위해 끝에 추가)

            // [추가] 이 구멍과 짝지어진 중심선(HOLE_DATA_GROUP_ID)을 저장/로드 후에도
            // 찾을 수 있도록 태그를 보존한다.
            const QString holeGid = item->data(HOLE_DATA_GROUP_ID).toString();
            if (!holeGid.isEmpty()) propObj.insert("holeGroupId", holeGid);
        }
    } else if (type == "Leader") {
        LeaderItem *ld = dynamic_cast<LeaderItem *>(item);
        if (ld) {
            const MyLeader &d = ld->leaderData();
            out << d.text
                << d.textSize
                << d.landingLength
                << (bool)d.hideArrow;
            out << (quint32)d.points.size();
            for (int i = 0; i < d.points.size(); ++i) {
                out << (d.points[i].x - offset.x())
                    << (d.points[i].y - offset.y());
            }
            // [추가] 구멍 콜아웃 ↔ 구멍 연결 보존 (기존 바이너리 형식 호환을 위해 JSON 속성에 저장)
            if (!d.holeIds.isEmpty())
                propObj.insert("holeCalloutIds", QJsonArray::fromStringList(d.holeIds));
        }
    }
}

// ─────────────────────────────────────────────────────────
// 공통 역직렬화 함수 (이곳에서 JSON 파싱과 팩토리 호출을 담당합니다)
// ─────────────────────────────────────────────────────────
CadItem *FileManager::deserializeCadItem(const QString &type,
                                         const QJsonObject &propObj,
                                         QDataStream &in,
                                         const QPen &layerPen) {
    QGraphicsItem *newItem = nullptr;
    QPen pen = layerPen;

    if (type == "Line") {
        double x1, y1, x2, y2;
        in >> x1 >> y1 >> x2 >> y2;
        newItem = m_ctx->entityFactory()->createLine(QLineF(x1, y1, x2, y2), pen);
        // 다중선 그룹 태그 복원 (JSON → QGraphicsItem::setData)
        if (newItem) {
            const QString mlGid     = propObj["mlGroupId"].toString();
            const QString mlRole    = propObj["mlRole"].toString();
            const double  mlSpacing = propObj["mlSpacing"].toDouble();
            if (!mlGid.isEmpty())    newItem->setData(ML_DATA_GROUP_ID, mlGid);
            if (!mlRole.isEmpty())   newItem->setData(ML_DATA_ROLE,     mlRole);
            if (mlSpacing > 0.0)     newItem->setData(ML_DATA_SPACING,  mlSpacing);
            // 도어/창문 그룹 태그 복원
            const QString pdGid   = propObj["pdGroupId"].toString();
            const QString pdCLine = propObj["pdClosingLine"].toString();
            if (!pdGid.isEmpty())   newItem->setData(PD_DATA_GROUP_ID,     pdGid);
            if (!pdCLine.isEmpty()) newItem->setData(PD_DATA_CLOSING_LINE, pdCLine);
            // [추가] 구멍(Hole) 중심선 그룹 태그 복원
            const QString holeGid = propObj["holeGroupId"].toString();
            if (!holeGid.isEmpty()) newItem->setData(HOLE_DATA_GROUP_ID, holeGid);
            // [추가] 구멍 단면(HoleSectionTool) 그룹 태그 + 배치 정보 복원
            const QString holeSecGid = propObj["holeSectionGroupId"].toString();
            if (!holeSecGid.isEmpty()) {
                newItem->setData(HOLE_SECTION_GROUP_ID, holeSecGid);
                newItem->setData(HOLE_SECTION_TOP_X, propObj["holeSectionTopX"].toDouble());
                newItem->setData(HOLE_SECTION_TOP_Y, propObj["holeSectionTopY"].toDouble());
                newItem->setData(HOLE_SECTION_DIR_X, propObj["holeSectionDirX"].toDouble());
                newItem->setData(HOLE_SECTION_DIR_Y, propObj["holeSectionDirY"].toDouble());
                newItem->setData(HOLE_SECTION_MATERIAL_THICKNESS, propObj["holeSectionThickness"].toDouble());
            }
        }
    } else if (type == "Circle") {
        double cx, cy, r;
        in >> cx >> cy >> r;
        newItem = m_ctx->entityFactory()->createCircle(QPointF(cx, cy), r, pen);
        // 상세도 경계원 태그 복원
        if (newItem) {
            if (propObj.contains("detailRegionScale")) {
                double drScale = propObj["detailRegionScale"].toDouble();
                if (drScale > 0.0)
                    newItem->setData(DETAIL_REGION_SCALE, drScale);
            }
            const QString drLabel = propObj["detailRegionLabel"].toString();
            if (!drLabel.isEmpty())
                newItem->setData(DETAIL_REGION_LABEL, drLabel);
        }
    } else if (type == "Arc") {
        double cx, cy, r, sa, span;
        in >> cx >> cy >> r >> sa >> span;
        newItem =
            m_ctx->entityFactory()->createArc(QPointF(cx, cy), r, sa, span, pen);
        // 도어/창문 그룹 태그 복원
        if (newItem) {
            const QString pdGid = propObj["pdGroupId"].toString();
            if (!pdGid.isEmpty()) newItem->setData(PD_DATA_GROUP_ID, pdGid);
            const QString drLabel = propObj["detailRegionLabel"].toString();
            if (!drLabel.isEmpty())
                newItem->setData(DETAIL_REGION_LABEL, drLabel);
        }
    } else if (type == "Polyline") {
        bool closed;
        quint32 vCount;
        in >> closed >> vCount;
        std::vector<MyVector> vertices;
        for (quint32 i = 0; i < vCount; ++i) {
            double vx, vy;
            in >> vx >> vy;
            vertices.push_back(MyVector(vx, vy));
        }
        newItem = m_ctx->entityFactory()->createPolyline(vertices, closed, pen);
        // 도어/창문 그룹 태그 복원
        if (newItem) {
            const QString pdGid = propObj["pdGroupId"].toString();
            if (!pdGid.isEmpty()) newItem->setData(PD_DATA_GROUP_ID, pdGid);
        }
    } else if (type == "Text") {
        double tx, ty;
        in >> tx >> ty;
        QString content = propObj["content"].toString();
        QString fontFamily = propObj["fontFamily"].toString();
        double fontSize = propObj["fontSize"].toDouble();
        double rotation = propObj["rotation"].toDouble();
        QColor textColor = pen.color();

        QFont font(fontFamily, fontSize);
        // 서식(없으면 꺼짐) — 이전 버전 파일 호환
        font.setBold(propObj["fontBold"].toBool(false));
        font.setItalic(propObj["fontItalic"].toBool(false));
        font.setUnderline(propObj["fontUnderline"].toBool(false));
        newItem = m_ctx->entityFactory()->createText(content, QPointF(tx, ty), font,
                                                     textColor, rotation);
        if (newItem) {
            TextItem* textItem = dynamic_cast<TextItem*>(newItem);
            if (textItem) {
                if (propObj.contains("isAttribute")) textItem->setProperty("isAttribute", propObj["isAttribute"].toBool());
                if (propObj.contains("tag")) textItem->setProperty("tag", propObj["tag"].toString());
                if (propObj.contains("prompt")) textItem->setProperty("prompt", propObj["prompt"].toString());
            }
        }
    } else if (type == "Block") {
        QString bName;
        double bScaleX, bScaleY, bRotation;
        in >> bName >> bScaleX >> bScaleY >> bRotation;
        double ix, iy;
        in >> ix >> iy;
        quint32 childCount;
        in >> childCount;

        QList<CadItem *> children;
        for (quint32 i = 0; i < childCount; ++i) {
            QString childType;
            QString childLayerName;
            QByteArray childPropsJson;
            QByteArray childGeoData;

            in >> childType >> childLayerName >> childPropsJson >> childGeoData;

            QJsonDocument doc = QJsonDocument::fromJson(childPropsJson);
            QJsonObject childPropObj = doc.object();

            QDataStream childIn(&childGeoData, QIODevice::ReadOnly);

            Layer *childLayer = m_ctx->layerManager()->getLayer(childLayerName);
            QPen childPen;
            if (childLayer) {
                childPen.setColor(childLayer->color());
                childPen.setWidthF(childLayer->weight());
                if (m_ctx->lineTypeManager()) {
                    QVector<qreal> pattern = m_ctx->lineTypeManager()->getQtDashPattern(
                        childLayer->linetype());
                    if (!pattern.isEmpty()) {
                        childPen.setStyle(Qt::CustomDashLine);
                        childPen.setDashPattern(pattern);
                    } else {
                        childPen.setStyle(Qt::SolidLine);
                    }
                }
            } else {
                childPen.setColor(QColor(childPropObj["color"].toString()));
                childPen.setWidthF(childPropObj["weight"].toDouble());
            }
            childPen.setCosmetic(true);

            CadItem *child =
                deserializeCadItem(childType, childPropObj, childIn, childPen);
            if (child) {
                child->setLayerName(childLayerName);
                children.append(child);
            }
        }
        newItem =
            new BlockItem(children, MyVector(ix, iy), bName, bScaleX, bScaleY, bRotation);

    } else if (type == "Dimension") {
        // 치수선 기존 로직 100% 보존
        double x1, y1, x2, y2, lx, ly;
        in >> x1 >> y1 >> x2 >> y2 >> lx >> ly;

        QString text = propObj["content"].toString(); // [수정] 원본 텍스트 우선 로드
        if (text.isEmpty() && propObj.contains("Text")) text = propObj["Text"].toString();

        MyDimension data(MyVector(x1, y1), MyVector(x2, y2), MyVector(lx, ly), text);

        QString dimTypeStr = propObj["DimType"].toString();
        if (dimTypeStr == "Angular") data.dimensionType = DimensionType::Angular;
        else if (dimTypeStr == "Radial") data.dimensionType = DimensionType::Radial;
        else if (dimTypeStr == "Diametric") data.dimensionType = DimensionType::Diametric;
        else if (dimTypeStr == "HalfDimension") data.dimensionType = DimensionType::HalfDimension;
        else data.dimensionType = DimensionType::Linear;

        // [추가] 구버전 파일 호환성: 접두어가 없는 Radial/Diametric에 기본값 부여
        data.prefix = propObj["Prefix"].toString();
        // [수정] R/Ø 기호는 BaseDimensionItem::calculateTextLayout 이 측정값 앞에 자동으로
        // 붙이므로 prefix 에 기본값을 넣으면 "RR13" 처럼 이중 표기된다.
        // 이전 버전이 기본값으로 저장해 둔 파일도 중복 기호를 제거한다.
        if (data.dimensionType == DimensionType::Radial && data.prefix == "R") data.prefix.clear();
        else if (data.dimensionType == DimensionType::Diametric &&
                 (data.prefix == QString::fromUtf8("Ø") || data.prefix == QString(QChar(0x00D8)))) data.prefix.clear();
        data.suffix = propObj["Suffix"].toString();
        
        if (data.dimensionType == DimensionType::Angular && !in.atEnd()) {
            double e2x, e2y;
            in >> e2x >> e2y;
            data.end2 = MyVector(e2x, e2y);
        } else if (propObj.contains("End2X")) {
            // [폴백] 바이너리에 없으면 JSON에서 시도
            data.end2.x = propObj["End2X"].toDouble();
            data.end2.y = propObj["End2Y"].toDouble();
        }

        data.tolType = static_cast<ToleranceType>(propObj["TolType"].toInt());
        data.tolUpper = propObj["TolUpper"].toDouble();
        data.tolLower = propObj["TolLower"].toDouble();
        data.fitCode = propObj["FitCode"].toString(); // [신규] 끼워맞춤 등급기호
        data.fitShowValues = propObj["FitShowValues"].toBool(); // [신규] 숫자 상/하한 병기 여부
        data.tolPrecision = propObj["TolPrecision"].toInt();

        data.hideExt1 = propObj["HideExt1"].toBool();
        data.hideExt2 = propObj["HideExt2"].toBool();
        data.extChainH1 = propObj["ExtChainH1"].toDouble();
        data.extChainH2 = propObj["ExtChainH2"].toDouble();
        data.useFixedAngle = propObj["UseFixedAngle"].toBool();
        data.fixedAngle = propObj["FixedAngle"].toDouble();

        data.hideArrow1 = propObj["HideArrow1"].toBool();
        data.hideArrow2 = propObj["HideArrow2"].toBool();
        data.isFullCircle = propObj["IsFullCircle"].toBool(); // [추가]
        data.useCustomTextPos = propObj["UseCustomTextPos"].toBool();
        data.customTextPos.x = propObj["CustomTextPosX"].toDouble();
        data.customTextPos.y = propObj["CustomTextPosY"].toDouble();

        // [DIMLFAC] 측정값 표시 배율 (구버전 파일엔 없음 → 1.0)
        data.measureScale = propObj.contains("MeasureScale")
                                ? propObj["MeasureScale"].toDouble()
                                : 1.0;
        if (data.measureScale <= 0.0)
            data.measureScale = 1.0;

        newItem = m_ctx->entityFactory()->createDimension(data, pen);

    } else if (type == "Spline") {
        qint32 degree;
        bool closed;
        quint32 vCount, kCount;
        in >> degree >> closed >> vCount;
        std::vector<MyVector> vertices;
        for (quint32 i = 0; i < vCount; ++i) {
            double vx, vy;
            in >> vx >> vy;
            vertices.push_back(MyVector(vx, vy));
        }
        in >> kCount;
        std::vector<double> knots;
        for (quint32 i = 0; i < kCount; ++i) {
            double k;
            in >> k;
            knots.push_back(k);
        }
        MySpline s(vertices, degree, closed);
        s.knots = knots;
        newItem = m_ctx->entityFactory()->createSpline(s, pen);

    } else if (type == "Ellipse") {
        double cx, cy, mx, my, ratio, sp, ep;
        in >> cx >> cy >> mx >> my >> ratio >> sp >> ep;
        MyEllipse e(MyVector(cx, cy), MyVector(mx, my), ratio, sp, ep);
        newItem = m_ctx->entityFactory()->createEllipse(e, pen);

    } else if (type == "Hatch") {
        QPainterPath path;
        in >> path;
        // 바이너리 스트림에서 직접 읽기 (JSON 파싱보다 우선순위 높음)
        QString pattern;
        double scale = 1.0;
        double angle = 0.0;
        in >> pattern >> scale >> angle;
        // 바이너리 스트림 읽기가 실패했을 경우(구버전 파일) JSON에서 폴백
        if (pattern.isEmpty()) {
            pattern = propObj["Pattern"].toString();
        }
        if (scale <= 0.0) {
            scale = propObj["Scale"].toDouble();
            if (scale <= 0.0) scale = 1.0;
        }
        newItem = m_ctx->entityFactory()->createHatch(path, pattern, scale, angle, pen);

        // ── [신규 통합] 기하공차 및 데이텀 역직렬화 ──
    } else if (type == "Gdt") {
        double px, py;
        qint32 sym;
        QString tolerance;
        bool hasDiam;
        double textH;
        in >> px >> py >> sym >> tolerance >> hasDiam >> textH;
        quint32 datumCount;
        in >> datumCount;

        GdtData d;
        d.position = MyVector(px, py);
        d.symbol = static_cast<GdtSymbol>(sym);
        d.toleranceValue = tolerance;
        d.hasDiameter = hasDiam;
        d.textHeight = textH;
        for (quint32 i = 0; i < datumCount; ++i) {
            QString datum;
            in >> datum;
            d.datumRefs.append(datum);
        }

        // 지시선 및 회전 로드
        in >> d.rotation >> d.useLeader;
        quint32 leaderCount;
        in >> leaderCount;
        for (quint32 i = 0; i < leaderCount; ++i) {
            double lx, ly;
            in >> lx >> ly;
            d.leaderPoints.append(MyVector(lx, ly));
        }
        newItem = m_ctx->entityFactory()->createGdt(d, pen);

    } else if (type == "Datum") {
        DatumData d;
        double sx, sy, ex, ey;
        in >> d.text >> sx >> sy >> ex >> ey >> d.textHeight;
        d.startPoint = MyVector(sx, sy);
        d.endPoint = MyVector(ex, ey);

        // [신규] 다중 지시선 로드 (데이터가 있을 경우)
        if (!in.atEnd()) {
            quint32 leaderCount;
            in >> leaderCount;
            for (quint32 i = 0; i < leaderCount; ++i) {
                double lx, ly;
                in >> lx >> ly;
                d.leaderPoints.append(MyVector(lx, ly));
            }
        }
        
        // 데이터가 없거나 비어있는 경우 하위 호환성 위해 기본값 생성
        if (d.leaderPoints.isEmpty()) {
            d.leaderPoints << d.startPoint << d.endPoint;
        }

        newItem = m_ctx->entityFactory()->createDatum(d, pen);

    } else if (type == "SurfaceFinish") {
        double px, py;
        qint32 sym;
        qint32 grade;
        bool tailBar, useLeaderVal;
        double textH, rotationVal;
        QString ra, method, cutoff, lay, allowance;
        in >> px >> py >> sym >> ra >> method >> cutoff >> lay >> allowance
           >> grade >> tailBar >> textH >> rotationVal >> useLeaderVal;

        SurfaceFinishData d;
        d.position = MyVector(px, py);
        d.symbol = static_cast<SurfaceFinishSymbol>(sym);
        d.raValue = ra;
        d.productionMethod = method;
        d.cutoffValue = cutoff;
        d.laySymbol = lay;
        d.machiningAllowance = allowance;
        d.legacyGrade = grade;
        d.showTailBar = tailBar;
        d.textHeight = textH;
        d.rotation = rotationVal;
        d.useLeader = useLeaderVal;

        quint32 leaderCount;
        in >> leaderCount;
        for (quint32 i = 0; i < leaderCount; ++i) {
            double lx, ly;
            in >> lx >> ly;
            d.leaderPoints.append(MyVector(lx, ly));
        }

        // [신규] W/X/Y/Z 약식 등급 문자 (구버전 파일에는 없을 수 있음)
        if (!in.atEnd()) {
            in >> d.raLetterCode;
        }
        // [신규] 기호/문자 전용 레이어 (구버전 파일에는 없을 수 있음)
        if (!in.atEnd()) {
            in >> d.symbolLayer >> d.textLayer;
        }

        newItem = m_ctx->entityFactory()->createSurfaceFinish(d, pen);
    } else if (type == "Hole") {
        MyHole d;
        double px, py;
        qint32 style;
        in >> px >> py
           >> d.diameter
           >> d.throughHole
           >> d.depth
           >> d.isThreaded
           >> d.threadSpec
           >> d.threadClass
           >> d.threadDepth
           >> d.toleranceClass
           >> d.hasCounterbore
           >> d.cbDiameter
           >> d.cbDepth
           >> d.hasCountersink
           >> d.csDiameter
           >> d.csAngle
           >> style
           >> d.showCenterLine
           >> d.baseLayer
           >> d.threadLayer
           >> d.cbLayer
           >> d.csLayer;
        d.position = MyVector(px, py);
        d.symbolStyle = static_cast<HoleSymbolStyle>(style);

        // [신규] 반원 표시 모드 (구버전 파일에는 없을 수 있음)
        if (!in.atEnd()) {
            qint32 renderModeVal = 0;
            in >> renderModeVal;
            d.renderMode = static_cast<HoleRenderMode>(renderModeVal);
        }

        newItem = m_ctx->entityFactory()->createHole(d, pen);
        // [추가] 구멍 중심선 그룹 태그 복원
        if (newItem) {
            const QString holeGid = propObj["holeGroupId"].toString();
            if (!holeGid.isEmpty()) newItem->setData(HOLE_DATA_GROUP_ID, holeGid);
        }
    } else if (type == "Leader") {
        MyLeader d;
        QString text;
        double textSize = 2.5, landingLen = 5.0;
        bool hideArrow = false;
        quint32 cnt = 0;
        in >> text >> textSize >> landingLen >> hideArrow >> cnt;
        d.text = text;
        d.textSize = textSize;
        d.landingLength = landingLen;
        d.hideArrow = hideArrow;
        for (quint32 i = 0; i < cnt; ++i) {
            double px, py;
            in >> px >> py;
            d.points.append(MyVector(px, py));
        }
        // [추가] 구멍 콜아웃 ↔ 구멍 연결 복원
        const QJsonArray holeIds = propObj["holeCalloutIds"].toArray();
        for (int i = 0; i < holeIds.size(); ++i)
            d.holeIds.append(holeIds.at(i).toString());
        newItem = m_ctx->entityFactory()->createLeader(d, pen);
    }

    if (newItem) {
        // [상세도] role/label 태그 복원 (모든 타입 공통)
        if (propObj.contains("detailRegionRole")) {
            int r = propObj["detailRegionRole"].toInt();
            if (r != 0) newItem->setData(DETAIL_REGION_ROLE, r);
        }
        {
            const QString drLabel = propObj["detailRegionLabel"].toString();
            if (!drLabel.isEmpty()) newItem->setData(DETAIL_REGION_LABEL, drLabel);
        }
        CadItem *ci = dynamic_cast<CadItem *>(newItem);
        return ci;
    }
    return nullptr;
}