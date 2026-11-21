#ifndef MODEL3DEXPORT_H
#define MODEL3DEXPORT_H

#include <QString>

struct Model3D;

// ─────────────────────────────────────────────────────────────────────────────
// Model3DExport — 3D 모델을 OBJ(+MTL)로 내보내고 Blender 로 연다
//
//   OBJ 규칙: 단위 m(도면 mm ÷ 1000), 평면 중심을 원점으로, 바닥 = 모델 최저 높이.
//   축: OBJ 관례(Y 위쪽)로 기록 → Blender 기본 OBJ 가져오기(앞 -Z, 위 Y)에서 Z 위쪽으로 복원.
//   객체: "벽_<레이어>", "개구부벽_<레이어>", "창유리" / 재질: Wall, Glass
//
//   Blender: <obj이름>_blender.py 를 같이 만들어 blender.exe --python 으로 실행
//   (기본 장면 비우기 → 가져오기 → 유리 투명·바닥·태양광·카메라 → 화면 맞춤).
//   스크립트에 "-- --render <png>" 를 주면 창 없이 렌더만 하고 끝낸다(검증용).
// ─────────────────────────────────────────────────────────────────────────────
class Model3DExport {
public:
    static bool writeObj(const Model3D &model, const QString &objPath, QString *error);
    static bool writeBlenderScript(const QString &objPath, QString *scriptPath, QString *error);

    // 저장된 경로 → Program Files\Blender Foundation\*\blender.exe(최신) 순으로 찾는다. 없으면 빈 문자열
    static QString findBlenderExe();
    static void setBlenderExe(const QString &path);

    // OBJ 를 쓴 뒤 Blender 를 띄운다 (blenderExe 가 비면 findBlenderExe)
    static bool openInBlender(const QString &objPath, const QString &blenderExe, QString *error);

    // 임시 폴더의 기본 OBJ 경로 (%TEMP%/AutoDraft3D/<baseName>.obj)
    static QString tempObjPath(const QString &baseName);
};

#endif // MODEL3DEXPORT_H
