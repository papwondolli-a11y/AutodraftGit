#ifndef MODEL3DVIEW_H
#define MODEL3DVIEW_H

#include "buildingmodel3d.h"

#include <QMatrix4x4>
#include <QOpenGLFunctions>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector3D>
#include <vector>

class QOpenGLShaderProgram;

// ─────────────────────────────────────────────────────────────────────────────
// Model3DView — Model3D(벽 돌출 덩어리 + 개구부)를 그리는 3D 뷰 (로드맵 3단계)
//   좌표: 도면 X·Y(mm) + Z(높이, mm, 위쪽 +)
//   마우스: 왼쪽 드래그 = 회전, 오른쪽/가운데 드래그 = 이동, 휠 = 확대/축소
// ─────────────────────────────────────────────────────────────────────────────
class Model3DView : public QOpenGLWidget, protected QOpenGLFunctions {
    Q_OBJECT
public:
    enum Preset { Iso = 0, Top, Front, Side };

    explicit Model3DView(QWidget *parent = nullptr);
    ~Model3DView() override;

    void setModel(const Model3D &model, bool resetCamera);
    void setPreset(Preset p);
    void setOrbit(double yawDeg, double pitchDeg); // 카메라 방향만 바꾼다 (MCP 확인용)
    double yawDeg() const { return m_yawDeg; }     // 등각도 그리기: 지금 보는 방향
    double pitchDeg() const { return m_pitchDeg; }

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void mouseReleaseEvent(QMouseEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;

private:
    struct Mesh {
        std::vector<float> pos;  // xyz
        std::vector<float> nrm;  // xyz
        std::vector<float> col;  // rgba
        void clear() { pos.clear(); nrm.clear(); col.clear(); }
        int count() const { return int(pos.size() / 3); }
    };

    void rebuildMesh();
    void addTri(Mesh &m, const QVector3D &a, const QVector3D &b, const QVector3D &c,
                const QVector3D &n, const QColor &color);
    void addQuad(Mesh &m, const QVector3D &a, const QVector3D &b, const QVector3D &c,
                 const QVector3D &d, const QVector3D &n, const QColor &color);
    void addLine(const QVector3D &a, const QVector3D &b);
    void addCap(const QList<QPolygonF> &rings, double z, bool up, const QColor &color);
    void addSolid(const Model3DSolid &s, const QColor &color);
    QVector3D toLocal(double x, double y, double z) const;
    QMatrix4x4 viewProjection() const;
    void drawMesh(const Mesh &m, int mode, bool lit);

    Model3D m_model;
    Mesh m_tris;   // 불투명 면
    Mesh m_glass;  // 반투명 유리
    Mesh m_lines;  // 모서리선
    QPointF m_center;    // 모델 평면 중심 (정밀도 위해 원점으로 옮김)
    double m_baseZ = 0.0;
    // 기계 부품만 있는 모델: 도면(XY)에 그린 부품을 정면도처럼 세워 보여 줌
    //   도면 X → 보기 X, 도면 Y(위) → 보기 위(Z), 도면 Z(보는 쪽) → 보기 앞(−Y, "정면" 카메라 쪽)
    bool m_standUp = false;
    double m_centerZ = 0.0;   // 세울 때: 도면 Z 가운데 (앞뒤 중심)
    double m_baseY = 0.0;     // 세울 때: 도면 Y 최솟값 (바닥에 닿는 높이)
    double m_radius = 10000.0;

    // 궤도 카메라
    QVector3D m_target;
    double m_distance = 20000.0;
    double m_yawDeg = -60.0;
    double m_pitchDeg = 35.0;
    QPoint m_lastMouse;
    Qt::MouseButton m_dragButton = Qt::NoButton;

    QOpenGLShaderProgram *m_program = nullptr;
    bool m_glReady = false;
};

#endif // MODEL3DVIEW_H
