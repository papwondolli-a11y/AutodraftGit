# AutoDraft

Qt/C++ 기반 2D CAD. 기계·건축 도면 작업을 지원하며 DXF/DWG 입출력과 독자 포맷(.cdb)을 사용한다.

> **처음 사용하는 분은 [START_HERE.md](START_HERE.md)부터 읽으세요.** (Claude 데스크탑 앱으로 새 툴을 만드는 방법)

## 개발 환경
- Qt 6.11.0 (MinGW 64bit), C++17
- Windows

## 빌드
- Qt Creator에서 `AutoDraftGit.pro`를 열어 빌드한다(qmake).
- CMake: `cmake -B build -DQT_ROOT=<Qt 설치 경로>` (기본값 `C:/Qt/6.11.0/mingw_64`)
- 새 `.cpp`/`.h`를 추가하면 **`AutoDraftGit.pro`와 `CMakeLists.txt` 양쪽에 등록**한다.
- DWG 기능은 `tools/` 의 변환기 실행 파일이 필요하다. 빌드 방법은 `tools/README.md` 참고.

## 구성
- `tools/` : DWG 변환기(`dwg2dxf.exe`, `dxf2dwg.exe`)와 그 소스. acadrust 기반
- `acadrustimporter.*`, `dwgexporter.*` : 변환기를 호출하는 DWG 가져오기·저장 진입점
- `Data/`, `blocks/`, `frames/`, `icons/`, `help/`, `translations/` : 실행에 필요한 리소스
- `ai_bridge/` : MCP 서버(`autodraft_mcp.py`)와 기본 배치(`layouts/default_32.json`). Claude가 AutoDraft를 조작할 때 사용한다.

## 배포용 ZIP 만들기
```
scripts\make_release.bat 1.0
```
`release_out\AutoDraft-v1.0-win64.zip`이 만들어진다. 개발 도구 없이 압축을 풀고 `AutoDraft\AutoDraft.exe`를 더블클릭하면 실행된다. ZIP 안의 `AutoDraft` 폴더에는 프로그램과 함께 `README.txt`, `TERMS.txt`(무료 사용·재배포 가능), `NOTICE.md`가 들어 있다. 소스 저장소가 비공개이면 이 ZIP을 **공개 저장소의 Releases**에 올려 배포한다(비공개 저장소의 Release는 공개할 수 없다).

## 서드파티
Qt 6(LGPLv3), acadrust(MPL-2.0), ACadSharp(MIT)를 사용한다. 자세한 내용은 [NOTICE.md](NOTICE.md) 참고.
