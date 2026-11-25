#include "isodrawing.h"

#include "model3dexport.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMap>
#include <QProcess>
#include <QStandardPaths>
#include <QTextStream>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

// Blender 쪽 스크립트: 가져오기 스크립트를 돌린 뒤(불리언 수정자 구성) 나사커터만 끄고, 평가된 메시를 mm 로 적는다
//   %1 = 가져오기 스크립트, %2 = 결과 파일
const char *kIsoScript = R"PY(# AutoDraft 등각도용 메시 내보내기 (자동 생성)
import bpy, runpy, re
SCRIPT = r"%1"
OUT = r"%2"
try:
    runpy.run_path(SCRIPT, run_name="__main__")
except SystemExit:
    pass
TOOL = re.compile(r"_(키홈커터|구멍커터|나사커터|합침|파내기)\d*$")
for o in bpy.data.objects:
    if o.type == 'MESH':
        for md in o.modifiers:
            if md.name.startswith("나사커터"):
                md.show_viewport = False
bpy.context.view_layer.update()
dg = bpy.context.evaluated_depsgraph_get()
with open(OUT, "w", encoding="utf-8") as f:
    for o in bpy.data.objects:
        if o.type != 'MESH' or TOOL.search(o.name) or o.name == "바닥":
            continue
        ev = o.evaluated_get(dg)
        me = ev.to_mesh()
        M = o.matrix_world
        f.write("o %s\n" % o.name)
        for v in me.vertices:
            w = M @ v.co
            f.write("v %.6f %.6f %.6f\n" % (w.x * 1000.0, w.y * 1000.0, w.z * 1000.0))
        me.calc_loop_triangles()
        for t in me.loop_triangles:
            f.write("f %d %d %d\n" % (t.vertices[0], t.vertices[1], t.vertices[2]))
        ev.to_mesh_clear()
print("ISO_DONE")
)PY";

// 투상된 삼각형 (가림 판정용)
struct Tri2 {
    QPointF a, b, c;
    double za = 0.0, zb = 0.0, zc = 0.0;   // 깊이 (클수록 보는 사람 쪽)
    QRectF box;
};

// 점 q 가 삼각형 안(가장자리에서 조금 안쪽)인가 → 그 자리 깊이
bool triDepthAt(const Tri2 &t, const QPointF &q, double &z)
{
    const double x0 = t.a.x(), y0 = t.a.y();
    const double d1x = t.b.x() - x0, d1y = t.b.y() - y0, d2x = t.c.x() - x0, d2y = t.c.y() - y0;
    const double den = d1x * d2y - d1y * d2x;
    if (std::fabs(den) < 1e-18)
        return false;
    const double px = q.x() - x0, py = q.y() - y0;
    const double u = (px * d2y - py * d2x) / den;
    const double v = (d1x * py - d1y * px) / den;
    const double w = 1.0 - u - v;
    const double e = 1e-6;
    if (u <= e || v <= e || w <= e)
        return false;
    z = t.za * w + t.zb * u + t.zc * v;
    return true;
}

// 합칠 선 하나: 방향 u, 직선 거리 c, 직선 위 구간 t0~t1
struct Item {
    double t0, t1, c;
    QPointF u;
};

bool itemLess(const Item &p, const Item &q)
{
    return p.t0 < q.t0;
}

// 같은 직선 위에서 이어지거나 겹치는 선을 하나로 (방향·직선 거리로 묶어 정렬 후 합침)
void mergeLines(QList<QLineF> &lines, double tol)
{
    QMap<QPair<qint64, qint64>, QList<Item>> buckets;
    for (int i = 0; i < lines.size(); ++i) {
        const QLineF &ln = lines.at(i);
        double dx = ln.dx(), dy = ln.dy();
        const double L = std::hypot(dx, dy);
        if (L < tol)
            continue;
        dx /= L;
        dy /= L;
        if (dx < -1e-12 || (std::fabs(dx) <= 1e-12 && dy < 0.0)) {
            dx = -dx;
            dy = -dy;
        }
        const QPointF u(dx, dy);
        const double c = -dy * ln.x1() + dx * ln.y1();
        const double ta = dx * ln.x1() + dy * ln.y1(), tb = dx * ln.x2() + dy * ln.y2();
        const double ang = std::atan2(dy, dx);
        const QPair<qint64, qint64> key(qint64(std::llround(ang / 1e-4)), qint64(std::llround(c / (tol * 4.0))));
        Item it;
        it.t0 = std::min(ta, tb);
        it.t1 = std::max(ta, tb);
        it.c = c;
        it.u = u;
        buckets[key] << it;
    }
    QList<QLineF> out;
    for (QMap<QPair<qint64, qint64>, QList<Item>>::iterator b = buckets.begin(); b != buckets.end(); ++b) {
        QList<Item> &items = b.value();
        std::sort(items.begin(), items.end(), itemLess);
        Item cur = items.first();
        for (int i = 1; i <= items.size(); ++i) {
            if (i < items.size() && items.at(i).t0 <= cur.t1 + tol) {
                cur.t1 = std::max(cur.t1, items.at(i).t1);
                continue;
            }
            const QPointF n(-cur.u.y(), cur.u.x());
            const QPointF foot = n * cur.c;
            out << QLineF(foot + cur.u * cur.t0, foot + cur.u * cur.t1);
            if (i < items.size())
                cur = items.at(i);
        }
    }
    lines = out;
}

} // namespace

void IsoDrawing::isoAxes(QVector3D &view, QVector3D &up, double &scale)
{
    view = QVector3D(1.0f, 1.0f, 1.0f).normalized();   // 오른쪽·위·앞에서 내려다봄 (정면·우측면·윗면이 보임)
    up = QVector3D(0.0f, 1.0f, 0.0f);
    scale = std::sqrt(1.5);                              // 등각도: 축 방향 실제 길이
}

void IsoDrawing::orbitAxes(double yawDeg, double pitchDeg, bool standUp, QVector3D &view, QVector3D &up, double &scale)
{
    const double yaw = qDegreesToRadians(yawDeg), pitch = qDegreesToRadians(pitchDeg);
    view = QVector3D(float(std::cos(pitch) * std::cos(yaw)), float(std::cos(pitch) * std::sin(yaw)),
                     float(std::sin(pitch)));
    up = QVector3D(0.0f, 0.0f, 1.0f);                    // 3D 창과 같은 위쪽
    if (standUp) {                                       // 창이 부품을 세워 보여 줌 → 창 방향을 모델 좌표로 되돌림
        view = QVector3D(view.x(), view.z(), -view.y());
        up = QVector3D(0.0f, 1.0f, 0.0f);
    }
    scale = 1.0;
}

bool IsoDrawing::blenderMeshes(const Model3D &model, QList<IsoMesh> &out, QString *error)
{
    out.clear();
    const QString exe = Model3DExport::findBlenderExe();
    if (exe.isEmpty() || !QFileInfo::exists(exe)) {
        if (error)
            *error = QStringLiteral("Blender 를 찾지 못했습니다. 부품 3D 보기의 [Blender로 열기]로 위치를 한 번 지정하세요.");
        return false;
    }
    // 3D 보기용 표시판(Keyway)은 빼고 보냄 — 실제 구멍 모서리와 겹치는 가짜 선이 생기지 않게
    Model3D m = model;
    for (int i = 0; i < m.meshes.size(); ++i) {
        QList<Model3DMeshGroup> keep;
        for (int g = 0; g < m.meshes.at(i).groups.size(); ++g)
            if (m.meshes.at(i).groups.at(g).material != QLatin1String("Keyway"))
                keep << m.meshes.at(i).groups.at(g);
        m.meshes[i].groups = keep;
    }
    const QString objPath = Model3DExport::tempObjPath(QStringLiteral("autodraft_iso"));
    if (!Model3DExport::writeObj(m, objPath, error))
        return false;
    QString importScript;
    if (!Model3DExport::writeBlenderScript(objPath, &importScript, error))
        return false;
    const QString dir = QFileInfo(objPath).absolutePath();
    const QString outPath = dir + QStringLiteral("/autodraft_iso_mesh.txt");
    const QString pyPath = dir + QStringLiteral("/autodraft_iso_export.py");
    QFile::remove(outPath);
    {
        QFile py(pyPath);
        if (!py.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
            if (error)
                *error = QStringLiteral("스크립트를 쓸 수 없습니다: %1").arg(pyPath);
            return false;
        }
        QTextStream ts(&py);
        ts << QString::fromUtf8(kIsoScript).arg(importScript, outPath);
    }
    QProcess proc;
    proc.start(exe, QStringList() << QStringLiteral("-b") << QStringLiteral("--factory-startup")
                                  << QStringLiteral("--python") << pyPath);
    if (!proc.waitForStarted(20000) || !proc.waitForFinished(300000)) {
        proc.kill();
        if (error)
            *error = QStringLiteral("Blender 실행이 끝나지 않았습니다.");
        return false;
    }
    QFile f(outPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (error)
            *error = QStringLiteral("Blender 결과가 없습니다.\n%1").arg(QString::fromLocal8Bit(proc.readAllStandardError().right(800)));
        return false;
    }
    QTextStream in(&f);
    IsoMesh cur;
    bool has = false;
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.startsWith(QLatin1String("o "))) {
            if (has && !cur.f.isEmpty())
                out << cur;
            cur = IsoMesh();
            cur.name = line.mid(2);
            has = true;
        } else if (line.startsWith(QLatin1String("v "))) {
            const QStringList p = line.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (p.size() >= 3) {
                // Blender 좌표 → 모델 좌표 (x 오른쪽, y 위, z 보는 쪽). 세워 내보낸 모델은 (X, −Z, Y) 로 돌아가 있으므로 되돌린다
                const float bx = p.at(0).toFloat(), by = p.at(1).toFloat(), bz = p.at(2).toFloat();
                cur.v << (model.standUp ? QVector3D(bx, bz, -by) : QVector3D(bx, by, bz));
            }
        } else if (line.startsWith(QLatin1String("f "))) {
            const QStringList p = line.mid(2).split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (p.size() >= 3)
                cur.f << p.at(0).toInt() << p.at(1).toInt() << p.at(2).toInt();
        }
    }
    if (has && !cur.f.isEmpty())
        out << cur;
    if (out.isEmpty() && error)
        *error = QStringLiteral("그릴 부품 메시가 없습니다.");
    return !out.isEmpty();
}

ProjDrawing IsoDrawing::build(const QList<IsoMesh> &meshes, const QVector3D &viewIn, const QVector3D &upIn, double scale,
                              bool showHidden, double featureDeg)
{
    ProjDrawing out;
    const QVector3D d = viewIn.normalized();
    QVector3D up = upIn;
    QVector3D r = QVector3D::crossProduct(up, d);
    if (r.length() < 1e-4f) {                            // 위쪽과 시선이 나란함 (바로 위에서 봄)
        up = QVector3D(0.0f, 1.0f, 0.0f);
        r = QVector3D::crossProduct(up, d);
        if (r.length() < 1e-4f)
            r = QVector3D(1.0f, 0.0f, 0.0f);
    }
    r.normalize();
    const QVector3D u = QVector3D::crossProduct(d, r).normalized();

    // 투상 삼각형 + 경계
    QVector<Tri2> tris;
    double minX = 1e300, minY = 1e300, maxX = -1e300, maxY = -1e300;
    for (int m = 0; m < meshes.size(); ++m) {
        const IsoMesh &me = meshes.at(m);
        for (int k = 0; k + 2 < me.f.size(); k += 3) {
            const int ia = me.f.at(k), ib = me.f.at(k + 1), ic = me.f.at(k + 2);
            if (ia < 0 || ib < 0 || ic < 0 || ia >= me.v.size() || ib >= me.v.size() || ic >= me.v.size())
                continue;
            const QVector3D A = me.v.at(ia), B = me.v.at(ib), C = me.v.at(ic);
            Tri2 t;
            t.a = QPointF(QVector3D::dotProduct(r, A) * scale, QVector3D::dotProduct(u, A) * scale);
            t.b = QPointF(QVector3D::dotProduct(r, B) * scale, QVector3D::dotProduct(u, B) * scale);
            t.c = QPointF(QVector3D::dotProduct(r, C) * scale, QVector3D::dotProduct(u, C) * scale);
            t.za = QVector3D::dotProduct(d, A) * scale;
            t.zb = QVector3D::dotProduct(d, B) * scale;
            t.zc = QVector3D::dotProduct(d, C) * scale;
            const double x0 = std::min({t.a.x(), t.b.x(), t.c.x()}), x1 = std::max({t.a.x(), t.b.x(), t.c.x()});
            const double y0 = std::min({t.a.y(), t.b.y(), t.c.y()}), y1 = std::max({t.a.y(), t.b.y(), t.c.y()});
            t.box = QRectF(QPointF(x0, y0), QPointF(x1, y1));
            minX = std::min(minX, x0);
            maxX = std::max(maxX, x1);
            minY = std::min(minY, y0);
            maxY = std::max(maxY, y1);
            tris << t;
        }
    }
    if (tris.isEmpty())
        return out;
    const double size = std::max(maxX - minX, maxY - minY);
    const double tolDepth = std::max(1e-6, size * 1e-5);
    const double tol = std::max(1e-6, size * 1e-6);

    // 격자 (가림 판정 후보 줄이기)
    const int N = std::max(16, std::min(256, int(std::sqrt(double(tris.size())) * 2.0)));
    const double cw = std::max(1e-9, (maxX - minX) / N), ch = std::max(1e-9, (maxY - minY) / N);
    QVector<QVector<int>> grid(N * N);
    for (int i = 0; i < tris.size(); ++i) {
        const QRectF &b = tris.at(i).box;
        const int gx0 = std::max(0, std::min(N - 1, int((b.left() - minX) / cw)));
        const int gx1 = std::max(0, std::min(N - 1, int((b.right() - minX) / cw)));
        const int gy0 = std::max(0, std::min(N - 1, int((b.top() - minY) / ch)));
        const int gy1 = std::max(0, std::min(N - 1, int((b.bottom() - minY) / ch)));
        for (int gy = gy0; gy <= gy1; ++gy)
            for (int gx = gx0; gx <= gx1; ++gx)
                grid[gy * N + gx] << i;
    }

    const double featureCos = std::cos(qDegreesToRadians(featureDeg));
    QList<QLineF> vis, hid;
    for (int m = 0; m < meshes.size(); ++m) {
        const IsoMesh &me = meshes.at(m);
        // 면 법선(double) + 변 → 면 목록
        //   불리언 결과에는 아주 가는 삼각형(슬리버)이 섞여 법선이 엉터리 → 가장 긴 변 건너 이웃 면의 법선을 물려받음
        QVector<QVector3D> fn;
        QVector<quint64> longest;                       // 면마다 가장 긴 변
        QVector<bool> sliver;
        QHash<quint64, QVector<int>> edges;
        for (int k = 0; k + 2 < me.f.size(); k += 3) {
            const int fi = k / 3;
            const int ia = me.f.at(k), ib = me.f.at(k + 1), ic = me.f.at(k + 2);
            if (ia < 0 || ib < 0 || ic < 0 || ia >= me.v.size() || ib >= me.v.size() || ic >= me.v.size()) {
                fn << QVector3D();
                longest << 0;
                sliver << true;
                continue;
            }
            const QVector3D A = me.v.at(ia), B = me.v.at(ib), C = me.v.at(ic);
            const double e1x = double(B.x()) - A.x(), e1y = double(B.y()) - A.y(), e1z = double(B.z()) - A.z();
            const double e2x = double(C.x()) - A.x(), e2y = double(C.y()) - A.y(), e2z = double(C.z()) - A.z();
            const double nx = e1y * e2z - e1z * e2y, ny = e1z * e2x - e1x * e2z, nz = e1x * e2y - e1y * e2x;
            const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
            const int vs[3] = {ia, ib, ic};
            double best = -1.0, maxLen2 = 0.0;
            quint64 bestKey = 0;
            for (int e = 0; e < 3; ++e) {
                const quint32 p = quint32(vs[e]), q = quint32(vs[(e + 1) % 3]);
                const quint64 key = (quint64(std::min(p, q)) << 32) | quint64(std::max(p, q));
                edges[key] << fi;
                const QVector3D dv = me.v.at(vs[(e + 1) % 3]) - me.v.at(vs[e]);
                const double l2 = double(dv.x()) * dv.x() + double(dv.y()) * dv.y() + double(dv.z()) * dv.z();
                maxLen2 = std::max(maxLen2, l2);
                if (l2 > best) {
                    best = l2;
                    bestKey = key;
                }
            }
            const bool thin = len < 1e-12 || (0.5 * len) / std::max(1e-30, maxLen2) < 1e-6;
            fn << (len < 1e-12 ? QVector3D() : QVector3D(float(nx / len), float(ny / len), float(nz / len)));
            longest << bestKey;
            sliver << thin;
        }
        for (int pass = 0; pass < 3; ++pass)
            for (int fi = 0; fi < fn.size(); ++fi) {
                if (!sliver.at(fi))
                    continue;
                const QVector<int> nb = edges.value(longest.at(fi));
                for (int j = 0; j < nb.size(); ++j)
                    if (nb.at(j) != fi && !sliver.at(nb.at(j))) {
                        fn[fi] = fn.at(nb.at(j));
                        sliver[fi] = false;
                        break;
                    }
            }
        for (QHash<quint64, QVector<int>>::const_iterator it = edges.constBegin(); it != edges.constEnd(); ++it) {
            const QVector<int> &fs = it.value();
            bool feature = false;
            if (fs.size() != 2) {
                feature = true;                         // 열린 변·여러 면이 만나는 변
            } else {
                const QVector3D n1 = fn.at(fs.at(0)), n2 = fn.at(fs.at(1));
                if (n1.isNull() || n2.isNull())
                    continue;
                if (QVector3D::dotProduct(n1, n2) < featureCos)
                    feature = true;                     // 꺾인 모서리
                else if ((QVector3D::dotProduct(n1, d) > 0.0f) != (QVector3D::dotProduct(n2, d) > 0.0f))
                    feature = true;                     // 굽은 면의 윤곽
            }
            if (!feature)
                continue;
            const int ia = int(it.key() >> 32), ib = int(it.key() & 0xffffffffu);
            const QVector3D A = me.v.at(ia), B = me.v.at(ib);
            const QPointF a(QVector3D::dotProduct(r, A) * scale, QVector3D::dotProduct(u, A) * scale);
            const QPointF b(QVector3D::dotProduct(r, B) * scale, QVector3D::dotProduct(u, B) * scale);
            const double za = QVector3D::dotProduct(d, A) * scale, zb = QVector3D::dotProduct(d, B) * scale;
            const double L = std::hypot(b.x() - a.x(), b.y() - a.y());
            if (L < tol)
                continue;                               // 시선과 나란한 변 (점)
            const int n = std::max(1, std::min(48, int(L / (size / 300.0)) + 1));
            QVector<int> st(n);
            for (int s = 0; s < n; ++s) {
                const double t = (s + 0.5) / n;
                const QPointF q = a + (b - a) * t;
                const double z = za + (zb - za) * t;
                bool occ = false;
                const int gx = std::max(0, std::min(N - 1, int((q.x() - minX) / cw)));
                const int gy = std::max(0, std::min(N - 1, int((q.y() - minY) / ch)));
                const QVector<int> &cell = grid.at(gy * N + gx);
                for (int c = 0; c < cell.size() && !occ; ++c) {
                    const Tri2 &tr = tris.at(cell.at(c));
                    if (!tr.box.contains(q))
                        continue;
                    double zt = 0.0;
                    if (triDepthAt(tr, q, zt) && zt > z + tolDepth)
                        occ = true;
                }
                st[s] = occ ? 2 : 1;
            }
            int s0 = 0;
            while (s0 < n) {
                int s1 = s0;
                while (s1 + 1 < n && st.at(s1 + 1) == st.at(s0))
                    ++s1;
                const QLineF piece(a + (b - a) * (double(s0) / n), a + (b - a) * (double(s1 + 1) / n));
                (st.at(s0) == 1 ? vis : hid) << piece;
                s0 = s1 + 1;
            }
        }
    }
    mergeLines(vis, std::max(1e-6, size * 1e-5));
    mergeLines(hid, std::max(1e-6, size * 1e-5));
    // 가운데를 (0, 0) 으로
    const QPointF c((minX + maxX) * 0.5, (minY + maxY) * 0.5);
    for (int i = 0; i < vis.size(); ++i)
        out.visible << vis.at(i).translated(-c);
    if (showHidden)
        for (int i = 0; i < hid.size(); ++i)
            out.hidden << hid.at(i).translated(-c);
    return out;
}
