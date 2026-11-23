#include "model3dview.h"
#include "model3dmesh.h"

#include <QMouseEvent>
#include <QOpenGLShaderProgram>
#include <QSurfaceFormat>
#include <QWheelEvent>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

const char *kVertexShader =
    "attribute vec3 aPos;\n"
    "attribute vec3 aNrm;\n"
    "attribute vec4 aCol;\n"
    "uniform mat4 uMvp;\n"
    "uniform vec3 uLight;\n"
    "uniform float uLit;\n"
    "varying vec4 vCol;\n"
    "void main() {\n"
    "  float d = abs(dot(normalize(aNrm), uLight));\n"
    "  float k = mix(1.0, 0.50 + 0.50 * d, uLit);\n"
    "  vCol = vec4(aCol.rgb * k, aCol.a);\n"
    "  gl_Position = uMvp * vec4(aPos, 1.0);\n"
    "}\n";

const char *kFragmentShader =
    "varying vec4 vCol;\n"
    "void main() {\n"
    "  gl_FragColor = vCol;\n"
    "}\n";

const QColor kWallColor(222, 218, 210);
const QColor kGlassColor(120, 170, 225, 90);
const QColor kFloorColor(205, 210, 216);
const QColor kLineColor(70, 70, 70);
const QColor kBackColor(236, 239, 243);

// 메시 부품 재질 색
QColor materialColor(const QString &material)
{
    if (material == QLatin1String("Steel"))
        return QColor(186, 192, 200);
    if (material == QLatin1String("Thread"))
        return QColor(120, 126, 136);
    if (material == QLatin1String("Keyway"))
        return QColor(55, 58, 64);
    return QColor(200, 200, 200);
}

} // namespace

Model3DView::Model3DView(QWidget *parent) : QOpenGLWidget(parent)
{
    // 앱 기본 포맷은 2D 도면용(깊이 버퍼 0)이므로 3D 뷰는 따로 깊이 버퍼를 요청한다
    QSurfaceFormat fmt = format();
    fmt.setDepthBufferSize(24);
    fmt.setSamples(4);
    setFormat(fmt);
    setMinimumSize(320, 240);
    setFocusPolicy(Qt::StrongFocus);
}

Model3DView::~Model3DView()
{
    makeCurrent();
    delete m_program;
    m_program = nullptr;
    doneCurrent();
}

// ── 모델 → 메시 ──────────────────────────────────────────────────────────────
void Model3DView::setModel(const Model3D &model, bool resetCamera)
{
    m_model = model;

    // 모델 범위
    double minX = 0, maxX = 0, minY = 0, maxY = 0, minZ = 0, maxZ = 0;
    bool first = true;
    for (int i = 0; i < m_model.solids.size(); ++i) {
        const Model3DSolid &s = m_model.solids.at(i);
        for (int r = 0; r < s.rings.size(); ++r) {
            const QPolygonF &ring = s.rings.at(r);
            for (int k = 0; k < ring.size(); ++k) {
                const QPointF p = ring.at(k);
                if (first) {
                    minX = maxX = p.x();
                    minY = maxY = p.y();
                    minZ = s.z0;
                    maxZ = s.z1;
                    first = false;
                }
                minX = std::min(minX, p.x());
                maxX = std::max(maxX, p.x());
                minY = std::min(minY, p.y());
                maxY = std::max(maxY, p.y());
                minZ = std::min(minZ, s.z0);
                maxZ = std::max(maxZ, s.z1);
            }
        }
    }
    // 메시 부품(기계용, 절삭체 제외)
    for (int i = 0; i < m_model.meshes.size(); ++i) {
        const Model3DMeshPart &part = m_model.meshes.at(i);
        if (!part.cutterFor.isEmpty())
            continue;
        for (int g = 0; g < part.groups.size(); ++g) {
            const QVector<QVector3D> &tris = part.groups.at(g).tris;
            for (int k = 0; k < tris.size(); ++k) {
                const QVector3D &v = tris.at(k);
                if (first) {
                    minX = maxX = v.x();
                    minY = maxY = v.y();
                    minZ = maxZ = v.z();
                    first = false;
                }
                minX = std::min(minX, double(v.x()));
                maxX = std::max(maxX, double(v.x()));
                minY = std::min(minY, double(v.y()));
                maxY = std::max(maxY, double(v.y()));
                minZ = std::min(minZ, double(v.z()));
                maxZ = std::max(maxZ, double(v.z()));
            }
        }
    }
    m_center = QPointF((minX + maxX) / 2.0, (minY + maxY) / 2.0);
    m_baseZ = minZ;
    // 기계 부품만 있으면(건축 벽 없음) 도면과 같은 방향으로 세운다 — 바닥에 눕지 않게
    m_standUp = m_model.solids.isEmpty() && !m_model.meshes.isEmpty();
    m_centerZ = (minZ + maxZ) / 2.0;
    m_baseY = minY;
    const double dx = maxX - minX, dy = maxY - minY, dz = maxZ - minZ;
    m_radius = std::max(10.0, 0.5 * std::sqrt(dx * dx + dy * dy + dz * dz));

    rebuildMesh();

    if (resetCamera)
        setPreset(Iso);
    else
        update();
}

QVector3D Model3DView::toLocal(double x, double y, double z) const
{
    if (m_standUp) {
        // (X, Y, Z)도면 → (X, −Z, Y)보기 : 회전이라 면 방향(법선)이 그대로 유지됨
        //   도면 Y 최솟값이 바닥(보기 Z = 0), 도면 Z 가운데가 앞뒤 중심
        return QVector3D(float(x - m_center.x()), float(-(z - m_centerZ)), float(y - m_baseY));
    }
    return QVector3D(float(x - m_center.x()), float(y - m_center.y()), float(z - m_baseZ));
}

void Model3DView::addTri(Mesh &m, const QVector3D &a, const QVector3D &b, const QVector3D &c,
                         const QVector3D &n, const QColor &color)
{
    const QVector3D v[3] = {a, b, c};
    for (int i = 0; i < 3; ++i) {
        m.pos.push_back(v[i].x());
        m.pos.push_back(v[i].y());
        m.pos.push_back(v[i].z());
        m.nrm.push_back(n.x());
        m.nrm.push_back(n.y());
        m.nrm.push_back(n.z());
        m.col.push_back(float(color.redF()));
        m.col.push_back(float(color.greenF()));
        m.col.push_back(float(color.blueF()));
        m.col.push_back(float(color.alphaF()));
    }
}

void Model3DView::addQuad(Mesh &m, const QVector3D &a, const QVector3D &b, const QVector3D &c,
                          const QVector3D &d, const QVector3D &n, const QColor &color)
{
    addTri(m, a, b, c, n, color);
    addTri(m, a, c, d, n, color);
}

void Model3DView::addLine(const QVector3D &a, const QVector3D &b)
{
    const QVector3D v[2] = {a, b};
    for (int i = 0; i < 2; ++i) {
        m_lines.pos.push_back(v[i].x());
        m_lines.pos.push_back(v[i].y());
        m_lines.pos.push_back(v[i].z());
        m_lines.nrm.push_back(0.0f);
        m_lines.nrm.push_back(0.0f);
        m_lines.nrm.push_back(1.0f);
        m_lines.col.push_back(float(kLineColor.redF()));
        m_lines.col.push_back(float(kLineColor.greenF()));
        m_lines.col.push_back(float(kLineColor.blueF()));
        m_lines.col.push_back(1.0f);
    }
}

// 구멍 있는 다각형 면(윗면/아랫면)을 삼각형으로 채운다 (Model3DMesh 공용 분할)
void Model3DView::addCap(const QList<QPolygonF> &rings, double z, bool up, const QColor &color)
{
    const QVector3D n(0.0f, 0.0f, up ? 1.0f : -1.0f);
    const QVector<QPointF> tris = Model3DMesh::triangulate(rings);
    for (int i = 0; i + 2 < tris.size(); i += 3) {
        addTri(m_tris, toLocal(tris.at(i).x(), tris.at(i).y(), z),
               toLocal(tris.at(i + 1).x(), tris.at(i + 1).y(), z),
               toLocal(tris.at(i + 2).x(), tris.at(i + 2).y(), z), n, color);
    }
}

void Model3DView::addSolid(const Model3DSolid &s, const QColor &color)
{
    if (s.z1 - s.z0 < 1e-6)
        return;
    addCap(s.rings, s.z1, true, color);
    addCap(s.rings, s.z0, false, color);
    for (int r = 0; r < s.rings.size(); ++r) {
        const QPolygonF &ring = s.rings.at(r);
        const int n = ring.size();
        for (int k = 0; k < n; ++k) {
            const QPointF p = ring.at(k);
            const QPointF q = ring.at((k + 1) % n);
            const QPointF d = q - p;
            const double len = std::sqrt(d.x() * d.x() + d.y() * d.y());
            if (len < 1e-6)
                continue;
            const QVector3D nrm(float(d.y() / len), float(-d.x() / len), 0.0f);
            const QVector3D a0 = toLocal(p.x(), p.y(), s.z0), b0 = toLocal(q.x(), q.y(), s.z0);
            const QVector3D b1 = toLocal(q.x(), q.y(), s.z1), a1 = toLocal(p.x(), p.y(), s.z1);
            addQuad(m_tris, a0, b0, b1, a1, nrm, color);
            addLine(a1, b1);
            addLine(a0, b0);

            // 세로 모서리: 앞뒤 변이 꺾이는 꼭짓점에만 (일직선 분할점은 생략)
            const QPointF o = ring.at((k - 1 + n) % n);
            const QPointF d0 = p - o;
            const double l0 = std::sqrt(d0.x() * d0.x() + d0.y() * d0.y());
            if (l0 > 1e-6) {
                const double crossv = (d0.x() * d.y() - d0.y() * d.x()) / (l0 * len);
                if (std::fabs(crossv) > 0.02)
                    addLine(a0, a1);
            }
        }
    }
}

void Model3DView::rebuildMesh()
{
    m_tris.clear();
    m_glass.clear();
    m_lines.clear();
    if (m_model.solids.isEmpty() && m_model.meshes.isEmpty())
        return;

    // 바닥판
    const double R = m_radius * 1.3;
    const QVector3D up(0.0f, 0.0f, 1.0f);
    const QVector3D f0(float(-R), float(-R), -2.0f), f1(float(R), float(-R), -2.0f);
    const QVector3D f2(float(R), float(R), -2.0f), f3(float(-R), float(R), -2.0f);
    addQuad(m_tris, f0, f1, f2, f3, up, kFloorColor);

    for (int i = 0; i < m_model.solids.size(); ++i)
        addSolid(m_model.solids.at(i), kWallColor);

    // 메시 부품 (기계용). 절삭체(키홈 커터)는 그리지 않는다.
    for (int i = 0; i < m_model.meshes.size(); ++i) {
        const Model3DMeshPart &part = m_model.meshes.at(i);
        if (!part.cutterFor.isEmpty())
            continue;
        for (int g = 0; g < part.groups.size(); ++g) {
            const Model3DMeshGroup &grp = part.groups.at(g);
            const QColor color = materialColor(grp.material);
            for (int k = 0; k + 2 < grp.tris.size(); k += 3) {
                const QVector3D a = toLocal(grp.tris.at(k).x(), grp.tris.at(k).y(), grp.tris.at(k).z());
                const QVector3D b = toLocal(grp.tris.at(k + 1).x(), grp.tris.at(k + 1).y(), grp.tris.at(k + 1).z());
                const QVector3D c = toLocal(grp.tris.at(k + 2).x(), grp.tris.at(k + 2).y(), grp.tris.at(k + 2).z());
                const QVector3D n = QVector3D::crossProduct(b - a, c - a).normalized();
                addTri(m_tris, a, b, c, n, color);
            }
        }
    }

    // 창: 벽 두께 가운데에 유리판
    for (int i = 0; i < m_model.openings.size(); ++i) {
        QPointF a, b;
        double z0 = 0.0, z1 = 0.0;
        if (!Model3DMesh::glassPane(m_model.openings.at(i), a, b, z0, z1))
            continue;
        const QPointF d = b - a;
        const QVector3D n(float(d.y()), float(-d.x()), 0.0f);
        addQuad(m_glass, toLocal(a.x(), a.y(), z0), toLocal(b.x(), b.y(), z0),
                toLocal(b.x(), b.y(), z1), toLocal(a.x(), a.y(), z1), n.normalized(), kGlassColor);
    }
}

// ── 카메라 ───────────────────────────────────────────────────────────────────
void Model3DView::setPreset(Preset p)
{
    m_target = QVector3D(0.0f, 0.0f, 0.0f);
    m_distance = m_radius * 2.4;
    switch (p) {
    case Iso:
        m_yawDeg = -60.0;
        m_pitchDeg = 35.0;
        break;
    case Top:
        m_yawDeg = -90.0;
        m_pitchDeg = 89.0;
        break;
    case Front:
        m_yawDeg = -90.0;
        m_pitchDeg = 5.0;
        break;
    case Side:
        m_yawDeg = 0.0;
        m_pitchDeg = 5.0;
        break;
    }
    update();
}

void Model3DView::setOrbit(double yawDeg, double pitchDeg)
{
    m_yawDeg = yawDeg;
    m_pitchDeg = qBound(-5.0, pitchDeg, 89.5);
    update();
}

QMatrix4x4 Model3DView::viewProjection() const
{
    const double yaw = qDegreesToRadians(m_yawDeg);
    const double pitch = qDegreesToRadians(m_pitchDeg);
    const QVector3D dir(float(std::cos(pitch) * std::cos(yaw)), float(std::cos(pitch) * std::sin(yaw)),
                        float(std::sin(pitch)));
    const QVector3D eye = m_target + dir * float(m_distance);

    QMatrix4x4 proj;
    const float aspect = height() > 0 ? float(width()) / float(height()) : 1.0f;
    proj.perspective(35.0f, aspect, float(m_distance * 0.02), float(m_distance + m_radius * 6.0));
    QMatrix4x4 view;
    view.lookAt(eye, m_target, QVector3D(0.0f, 0.0f, 1.0f));
    return proj * view;
}

// ── OpenGL ───────────────────────────────────────────────────────────────────
void Model3DView::initializeGL()
{
    initializeOpenGLFunctions();
    m_program = new QOpenGLShaderProgram();
    m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, kVertexShader);
    m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, kFragmentShader);
    m_program->bindAttributeLocation("aPos", 0);
    m_program->bindAttributeLocation("aNrm", 1);
    m_program->bindAttributeLocation("aCol", 2);
    m_glReady = m_program->link();
}

void Model3DView::resizeGL(int w, int h)
{
    Q_UNUSED(w);
    Q_UNUSED(h);
}

void Model3DView::drawMesh(const Mesh &m, int mode, bool lit)
{
    if (m.count() == 0)
        return;
    m_program->setUniformValue("uLit", lit ? 1.0f : 0.0f);
    m_program->enableAttributeArray(0);
    m_program->enableAttributeArray(1);
    m_program->enableAttributeArray(2);
    m_program->setAttributeArray(0, GL_FLOAT, m.pos.data(), 3);
    m_program->setAttributeArray(1, GL_FLOAT, m.nrm.data(), 3);
    m_program->setAttributeArray(2, GL_FLOAT, m.col.data(), 4);
    glDrawArrays(GLenum(mode), 0, m.count());
    m_program->disableAttributeArray(0);
    m_program->disableAttributeArray(1);
    m_program->disableAttributeArray(2);
}

void Model3DView::paintGL()
{
    glClearColor(float(kBackColor.redF()), float(kBackColor.greenF()), float(kBackColor.blueF()), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!m_glReady || m_tris.count() == 0)
        return;

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    m_program->bind();
    m_program->setUniformValue("uMvp", viewProjection());
    m_program->setUniformValue("uLight", QVector3D(0.45f, 0.30f, 0.84f).normalized());

    // 면 (모서리선이 면에 묻히지 않도록 살짝 뒤로)
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);
    drawMesh(m_tris, GL_TRIANGLES, true);
    glDisable(GL_POLYGON_OFFSET_FILL);

    drawMesh(m_lines, GL_LINES, false);

    // 유리 (반투명, 깊이 기록 안 함)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    drawMesh(m_glass, GL_TRIANGLES, false);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    m_program->release();
    glDisable(GL_DEPTH_TEST);
}

// ── 마우스 ───────────────────────────────────────────────────────────────────
void Model3DView::mousePressEvent(QMouseEvent *e)
{
    m_lastMouse = e->pos();
    m_dragButton = e->button();
}

void Model3DView::mouseMoveEvent(QMouseEvent *e)
{
    if (m_dragButton == Qt::NoButton)
        return;
    const QPoint d = e->pos() - m_lastMouse;
    m_lastMouse = e->pos();

    if (m_dragButton == Qt::LeftButton) {
        m_yawDeg -= d.x() * 0.4;
        m_pitchDeg = qBound(-5.0, m_pitchDeg + d.y() * 0.4, 89.5);
    } else {
        // 화면 평면으로 이동: 카메라 오른쪽·위 방향
        const double yaw = qDegreesToRadians(m_yawDeg);
        const double pitch = qDegreesToRadians(m_pitchDeg);
        const QVector3D fwd(float(-std::cos(pitch) * std::cos(yaw)), float(-std::cos(pitch) * std::sin(yaw)),
                            float(-std::sin(pitch)));
        const QVector3D right = QVector3D::crossProduct(fwd, QVector3D(0.0f, 0.0f, 1.0f)).normalized();
        const QVector3D upv = QVector3D::crossProduct(right, fwd).normalized();
        const double scale = m_distance * 0.0012;
        m_target += (-right * float(d.x()) + upv * float(d.y())) * float(scale);
    }
    update();
}

void Model3DView::mouseReleaseEvent(QMouseEvent *e)
{
    Q_UNUSED(e);
    m_dragButton = Qt::NoButton;
}

void Model3DView::wheelEvent(QWheelEvent *e)
{
    const double steps = e->angleDelta().y() / 120.0;
    m_distance = qBound(m_radius * 0.05, m_distance * std::pow(0.88, steps), m_radius * 20.0);
    update();
}
