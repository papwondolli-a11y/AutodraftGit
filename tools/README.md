# tools — DWG 변환기

AutoDraft는 DWG를 직접 읽고 쓰지 않고, acadrust 기반 변환기를 별도 프로세스로 실행한다.
변환기가 실패해도 본 프로그램은 영향을 받지 않는다.

| 실행 파일 | 소스 | 용도 | 사용하는 곳 |
|---|---|---|---|
| `dwg2dxf.exe` | `dwg2dxf/` | DWG → DXF | `acadrustimporter.cpp` (DWG 가져오기, 일괄 출력) |
| `dxf2dwg.exe` | `dxf2dwg/` | DXF → DWG | `dwgexporter.cpp` (DWG로 저장) |

## 사용법

```
dwg2dxf <in.dwg> <out.dxf> [--version r12|2000|2004|2007|2010|2013|2018]
dxf2dwg <in.dxf> <out.dwg> [--version r12|2000|2004|2007|2010|2013|2018] [--failsafe]
```

AutoDraft는 DWG 가져오기에서 `--version 2018`로 변환한다. R2007 미만 DXF는 텍스트가
시스템 코드페이지(CP949)로 저장되어 한글·지름 기호가 깨질 수 있기 때문이다.

## 빌드

Rust 도구체인(`cargo`)이 필요하다. 각 폴더에서:

```
cargo build --release
```

결과 `target/release/*.exe` 를 이 폴더(`tools/`)에 복사한다. 정적 CRT 설정이
`.cargo/config.toml` 에 있다.

## 라이선스

acadrust(MPL-2.0)와 ACadSharp(MIT)를 사용한다. 자세한 내용은 저장소 루트의 `NOTICE.md` 참고.
