#ifndef DXFWRITEHELPER_H
#define DXFWRITEHELPER_H

#include <QTextStream>
#include <QString>

// =============================================================================
// DXF 출력 공통 헬퍼 함수
// 각 핸들러의 exportDxf()에서 그룹코드+값을 한 줄씩 기록할 때 사용한다.
// DXF 텍스트 형식: [그룹코드]\n[값]\n
// =============================================================================

/// 문자열 값을 DXF 그룹코드 형식으로 출력한다.
inline void writeGC(QTextStream& out, int code, const QString& val) {
    out << code << "\n" << val << "\n";
}

/// 실수 값을 DXF 그룹코드 형식으로 출력한다.
inline void writeGC(QTextStream& out, int code, double val) {
    out << code << "\n" << val << "\n";
}

/// 정수 값을 DXF 그룹코드 형식으로 출력한다.
/// 그룹코드 70, 72, 73, 90 등 정수 전용 코드에 사용한다.
inline void writeGC(QTextStream& out, int code, int val) {
    out << code << "\n" << val << "\n";
}

#endif // DXFWRITEHELPER_H
