#include "overkillcommand.h"
#include "cadcommands.h"
#include "caditem.h"
#include "lineitem.h"
#include "basedimensionitem.h"
#include "mygraphicsscene.h"
#include "mymath.h"
#include "entityfactory.h"
#include <QDebug>
#include <QUndoCommand>
#include <cmath>
#include <algorithm>
#include <map>

// Helper structure for interval merging
struct Interval {
    double t1, t2;
    CadItem* item;
    bool merged = false;

    bool operator<(const Interval& other) const {
        if (std::abs(t1 - other.t1) > 1e-6)
            return t1 < other.t1;
        return t2 < other.t2;
    }
};

// Line signature for grouping collinear lines
struct LineSignature {
    double nx, ny, d;

    bool operator<(const LineSignature& other) const {
        if (std::abs(nx - other.nx) > 1e-6) return nx < other.nx;
        if (std::abs(ny - other.ny) > 1e-6) return ny < other.ny;
        if (std::abs(d - other.d) > 1e-6) return d < other.d;
        return false;
    }
};

OverkillCommand::OverkillCommand(IAppContext *ctx, QObject *parent)
    : CommandBase(parent), m_ctx(ctx) {}

void OverkillCommand::execute() {
    if (!m_ctx || !m_ctx->activeScene()) return;

    MyGraphicsScene* scene = m_ctx->activeScene();
    QList<QGraphicsItem*> allItems = scene->items();
    QList<CadItem*> cadItems;

    for (QGraphicsItem* item : std::as_const(allItems)) {
        if (CadItem* cad = dynamic_cast<CadItem*>(item)) {
            // Skip children that are logically part of another entity (like grips)
            // But include items whose parent is a layer root (not a CadItem)
            QGraphicsItem* parent = cad->parentItem();
            if (!parent || !dynamic_cast<CadItem*>(parent)) {
                cadItems.append(cad);
            }
        }
    }

    if (cadItems.isEmpty()) return;

    QUndoCommand* macro = new QUndoCommand("OVERKILL");

    // 1. Group lines by signature for collinear merging
    std::map<LineSignature, QList<LineItem*>> lineGroups;
    QList<CadItem*> otherItems;

    for (CadItem* item : cadItems) {
        if (LineItem* lineItem = dynamic_cast<LineItem*>(item)) {
            MyLine line = lineItem->getLine();
            MyVector v = line.end - line.start;
            double len = v.magnitude();
            if (len < MyMath::TOLERANCE) {
                // Remove zero-length lines
                new RemoveEntityCommand(scene, lineItem, macro);
                continue;
            }

            MyVector dir = v / len;
            MyVector normal(-dir.y, dir.x);
            
            // Normalize normal vector for consistent signature
            if (normal.x < -1e-6 || (std::abs(normal.x) < 1e-6 && normal.y < -1e-6)) {
                normal = normal * -1.0;
            }

            double d = MyMath::dotProduct(line.start, normal);
            
            LineSignature sig;
            sig.nx = normal.x;
            sig.ny = normal.y;
            sig.d = d;
            
            lineGroups[sig].append(lineItem);
        } else {
            otherItems.append(item);
        }
    }

    // 2.1 Dimension Extension Line Overlap Handling
    QList<BaseDimensionItem*> dimItems;
    for (int i = 0; i < cadItems.size(); ++i) { // C++11 index-based loop
        CadItem* item = cadItems[i];
        if (BaseDimensionItem* dim = dynamic_cast<BaseDimensionItem*>(item)) {
            dimItems.append(dim);
        }
    }

    for (int i = 0; i < dimItems.size(); ++i) {
        for (int j = i + 1; j < dimItems.size(); ++j) {
            BaseDimensionItem* d1 = dimItems[i];
            BaseDimensionItem* d2 = dimItems[j];
            if (d1->layerName() != d2->layerName()) continue;

            const MyDimension& data1 = d1->data();
            const MyDimension& data2 = d2->data();

            MyVector p1a = data1.start;
            MyVector p1b = data1.end;
            MyVector p2a = data2.start;
            MyVector p2b = data2.end;

            // lambda to check and hide if long encapsulates short
            auto checkAndHide = [&](BaseDimensionItem* dLong, int extIndexLong, BaseDimensionItem* dShort, int extIndexShort,
                                    const MyVector& shared, const MyVector& otherLong, const MyVector& otherShort)
            {
                Q_UNUSED(dLong)
                Q_UNUSED(extIndexLong)
                MyVector vLong = otherLong - shared;
                MyVector vShort = otherShort - shared;
                // If they go in the same direction and Long is truly longer
                if (MyMath::dotProduct(vLong, vShort) > 0.001) {
                    if (vLong.magnitude() > vShort.magnitude() + MyMath::TOLERANCE) {
                        const MyDimension& dataShort = dShort->data();
                        bool current = (extIndexShort == 1) ? dataShort.hideExt1 : dataShort.hideExt2;
                        if (!current) {
                            new ChangePropertyCommand(dShort, extIndexShort == 1 ? "HideExt1" : "HideExt2", current, true, macro);
                        }
                    }
                }
            };

            if (p1a.distanceTo(p2a) < MyMath::TOLERANCE) {
                checkAndHide(d1, 1, d2, 1, p1a, p1b, p2b);
                checkAndHide(d2, 1, d1, 1, p2a, p2b, p1b);
            } else if (p1a.distanceTo(p2b) < MyMath::TOLERANCE) {
                checkAndHide(d1, 1, d2, 2, p1a, p1b, p2a);
                checkAndHide(d2, 2, d1, 1, p2b, p2a, p1b);
            } else if (p1b.distanceTo(p2a) < MyMath::TOLERANCE) {
                checkAndHide(d1, 2, d2, 1, p1b, p1a, p2b);
                checkAndHide(d2, 1, d1, 2, p2a, p2b, p1a);
            } else if (p1b.distanceTo(p2b) < MyMath::TOLERANCE) {
                checkAndHide(d1, 2, d2, 2, p1b, p1a, p2a);
                checkAndHide(d2, 2, d1, 2, p2b, p2a, p1a);
            }
        }
    }

    // 2. Collinear Merging within groups
    for (auto it = lineGroups.begin(); it != lineGroups.end(); ++it) {
        const LineSignature& sig = it->first;
        QList<LineItem*>& group = it->second;

        if (group.isEmpty()) continue;

        // Sort items by layer and then by position
        // For simplicity, we only merge items on the same layer with same properties
        std::map<QString, QList<LineItem*>> layerGroups;
        for (LineItem* li : group) {
            layerGroups[li->layerName()].append(li);
        }

        for (auto lit = layerGroups.begin(); lit != layerGroups.end(); ++lit) {
            QList<LineItem*>& layerGroup = lit->second;
            if (layerGroup.size() < 1) continue;

            // Project onto a common axis (the line itself)
            MyVector dir(sig.ny, -sig.nx); // Direction is orthogonal to normal

            QList<Interval> intervals;
            for (LineItem* li : layerGroup) {
                MyLine line = li->getLine();
                double t1 = MyMath::dotProduct(line.start, dir);
                double t2 = MyMath::dotProduct(line.end, dir);
                if (t1 > t2) std::swap(t1, t2);
                intervals.append({t1, t2, li});
            }

            std::sort(intervals.begin(), intervals.end());

            // Merge overlapping or touching intervals
            QList<Interval> merged;
            if (!intervals.isEmpty()) {
                Interval current = intervals[0];
                for (int i = 1; i < intervals.size(); ++i) {
                    if (intervals[i].t1 <= current.t2 + MyMath::TOLERANCE) {
                        // Overlap or touch -> merge
                        if (intervals[i].t2 > current.t2) {
                            current.t2 = intervals[i].t2;
                        }
                        // Mark the original items for removal
                        new RemoveEntityCommand(scene, intervals[i].item, macro);
                        intervals[i].merged = true;
                    } else {
                        // No overlap -> finish current and start new
                        merged.append(current);
                        current = intervals[i];
                    }
                }
                merged.append(current);
            }

            // For each merged interval, if it was actually merged or replaced, create new item
            for (const auto& m : merged) {
                // Check if this interval is different from the original item's interval
                // Or if it was part of a merge. 
                // To keep it simple: if the count of items in this merge set > 1, replace.
                // But we need to know if the 'current' result is different from 'm.item'.
                
                // Find how many items were swallowed by this current interval
                int swallowCount = 0;
                for (LineItem* li : layerGroup) {
                    MyLine line = li->getLine();
                    double t1 = MyMath::dotProduct(line.start, dir);
                    double t2 = MyMath::dotProduct(line.end, dir);
                    if (t1 > t2) std::swap(t1, t2);
                    if (t1 >= m.t1 - 1e-6 && t2 <= m.t2 + 1e-6) {
                        swallowCount++;
                    }
                }

                if (swallowCount > 1) {
                    // Create a new line representing the merged segment
                    MyVector start = dir * m.t1 + MyVector(sig.nx * sig.d, sig.ny * sig.d);
                    MyVector end = dir * m.t2 + MyVector(sig.nx * sig.d, sig.ny * sig.d);
                    
                    LineItem* firstItem = dynamic_cast<LineItem*>(m.item);
                    QPen pen = firstItem->pen();
                    QString layer = firstItem->layerName();

                    LineItem* newLine = new LineItem(start, end);
                    newLine->setPen(pen);
                    newLine->setLayerName(layer);
                    
                    new RemoveEntityCommand(scene, m.item, macro);
                    new AddEntityCommand(scene, newLine, macro);
                }
            }
        }
    }

    // 3. Remove duplicates of other items
    // (Simple O(N^2) comparison for now, can be optimized later if needed)
    QSet<CadItem*> toRemove;
    for (int i = 0; i < otherItems.size(); ++i) {
        if (toRemove.contains(otherItems[i])) continue;
        for (int j = i + 1; j < otherItems.size(); ++j) {
            if (toRemove.contains(otherItems[j])) continue;
            
            CadItem* item1 = otherItems[i];
            CadItem* item2 = otherItems[j];

            const MyEntity* entity1 = item1->getEntityData();
            const MyEntity* entity2 = item2->getEntityData();
            if (!entity1 || !entity2) continue; // e.g. DatumItem has no MyEntity backing
            if (entity1->type() != entity2->type()) continue;
            if (item1->layerName() != item2->layerName()) continue;

            // Special comparison for specific types to be more robust than just QVariant properties
            bool match = false;
            EntityType type1 = entity1->type();
            if (type1 == EntityType::Circle || type1 == EntityType::Arc) {
                QMap<QString, QVariant> p1 = item1->getProperties();
                QMap<QString, QVariant> p2 = item2->getProperties();
                
                double dCenter = MyVector(p1["CenterX"].toDouble(), p1["CenterY"].toDouble())
                        .distanceTo(MyVector(p2["CenterX"].toDouble(), p2["CenterY"].toDouble()));
                double dRadius = std::abs(p1["Radius"].toDouble() - p2["Radius"].toDouble());
                
                match = (dCenter < MyMath::TOLERANCE && dRadius < MyMath::TOLERANCE);
                
                if (match && type1 == EntityType::Arc) {
                    double dStart = std::abs(p1["StartAngle"].toDouble() - p2["StartAngle"].toDouble());
                    double dSpan = std::abs(p1["SpanAngle"].toDouble() - p2["SpanAngle"].toDouble());
                    match = (dStart < 0.1 && dSpan < 0.1);
                }
            } else {
                // Generic property comparison
                QMap<QString, QVariant> props1 = item1->getProperties();
                QMap<QString, QVariant> props2 = item2->getProperties();
                
                if (props1.size() == props2.size()) {
                    match = true;
                    for (auto pit = props1.begin(); pit != props1.end(); ++pit) {
                        if (props2[pit.key()] != pit.value()) {
                            match = false;
                            break;
                        }
                    }
                }
            }

            if (match) {
                toRemove.insert(item2);
            }
        }
    }

    for (CadItem* item : toRemove) {
        new RemoveEntityCommand(scene, item, macro);
    }

    if (macro->childCount() > 0) {
        if (m_ctx->undoStack()) {
            m_ctx->undoStack()->push(macro);
        } else {
            macro->redo();
            delete macro;
        }
        scene->update();
    } else {
        delete macro;
    }
}
